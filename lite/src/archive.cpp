#include "archive.h"
#include "platform.h"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
extern "C" int tangos_inflate(const unsigned char *, size_t, unsigned char *, size_t);
namespace lite {
namespace {
struct WindowsPathLess {
  bool operator()(const std::wstring &a, const std::wstring &b) const {
    return CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) ==
           CSTR_LESS_THAN;
  }
};
struct Entry {
  std::string path, content;
  bool directory;
};
uint32_t crc32(const std::string &s) {
  uint32_t c = ~0u;
  for (unsigned char b : s) {
    c ^= b;
    for (int i = 0; i < 8; ++i)
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return ~c;
}
std::vector<Entry> decode(const fs::path &archive, Settings settings) {
  if (!fs::is_regular_file(archive) || fs::file_size(archive) > 128 * 1024 * 1024)
    throw std::runtime_error("ZIP must be a regular file under 128 MiB");
  auto data = read(archive);
  auto u16 = [&](size_t p) -> uint16_t {
    if (p > data.size() || data.size() - p < 2)
      throw std::runtime_error("Truncated ZIP");
    return uint8_t(data[p]) | (uint16_t(uint8_t(data[p + 1])) << 8);
  };
  auto u32 = [&](size_t p) -> uint32_t { return uint32_t(u16(p)) | (uint32_t(u16(p + 2)) << 16); };
  if (data.size() < 22)
    throw std::runtime_error("Invalid ZIP");
  size_t end = data.size() - 22;
  size_t lower = data.size() > 65557 ? data.size() - 65557 : 0;
  while (u32(end) != 0x06054b50 || end + 22 + u16(end + 20) != data.size()) {
    if (end == lower)
      throw std::runtime_error("ZIP central directory not found");
    --end;
  }
  auto count = u16(end + 10);
  auto central = u32(end + 16), centralSize = u32(end + 12);
  if (u16(end + 4) || u16(end + 6) || u16(end + 8) != count || count == 65535 || count > 20000 ||
      uint64_t(central) + centralSize != end)
    throw std::runtime_error("Split, ZIP64 or oversized ZIP is unsupported");
  std::vector<Entry> entries;
  std::set<std::wstring, WindowsPathLess> seen;
  uint64_t total = 0;
  size_t p = central;
  settings.portOnly = false; // Importing a fresh project is not a source repair.
  for (unsigned i = 0; i < count; ++i) {
    if (p > end || end - p < 46 || u32(p) != 0x02014b50)
      throw std::runtime_error("Invalid ZIP directory entry");
    auto flags = u16(p + 8), method = u16(p + 10);
    auto crc = u32(p + 16), packed = u32(p + 20), size = u32(p + 24);
    auto nameLength = u16(p + 28), extra = u16(p + 30), comment = u16(p + 32);
    auto attributes = u32(p + 38), local = u32(p + 42);
    size_t next = p + 46 + nameLength + extra + comment;
    if (next > end || !nameLength || (flags & ~uint16_t(0x080e)) || (method != 0 && method != 8) ||
        u16(p + 34) || ((attributes >> 16) & 0170000) == 0120000 || (attributes & 0x400))
      throw std::runtime_error("ZIP contains encrypted, linked or unsupported entries");
    auto name = data.substr(p + 46, nameLength);
    bool directory = name.back() == '/';
    if (directory)
      name.pop_back();
    if (name.empty() || name.front() == '/' || name.find_first_of("\\:\0", 0, 3) != name.npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
      throw std::runtime_error("Unsafe ZIP path");
    for (auto &part : split(name, '/')) {
      auto lowerPart = part;
      std::transform(lowerPart.begin(), lowerPart.end(), lowerPart.begin(), ::tolower);
      auto stem = lowerPart.substr(0, lowerPart.find('.'));
      bool device =
          stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
          (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
           stem[3] >= '0' && stem[3] <= '9');
      auto deviceStem = wide(stem);
      if (deviceStem.size() == 4 &&
          (deviceStem.substr(0, 3) == L"com" || deviceStem.substr(0, 3) == L"lpt") &&
          (deviceStem[3] == L'\u00b9' || deviceStem[3] == L'\u00b2' || deviceStem[3] == L'\u00b3'))
        device = true;
      if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' ||
          lowerPart == ".git" || device || part.find_first_of("<>\"|?*") != part.npos)
        throw std::runtime_error("Unsafe ZIP path: " + name);
    }
    auto folded = wide(name);
    if (!seen.insert(folded).second)
      throw std::runtime_error("Duplicate ZIP path: " + name);
    if (size > 16 * 1024 * 1024 || (total += size) > 256 * 1024 * 1024)
      throw std::runtime_error("ZIP uncompressed size exceeds safety limit");
    if (uint64_t(local) + 30 > central || u32(local) != 0x04034b50 || u16(local + 6) != flags ||
        u16(local + 8) != method)
      throw std::runtime_error("ZIP local header disagrees with central directory");
    auto localName = u16(local + 26), localExtra = u16(local + 28);
    uint64_t start = uint64_t(local) + 30 + localName + localExtra;
    if (start + packed > central ||
        data.substr(local + 30, localName) != data.substr(p + 46, nameLength))
      throw std::runtime_error("Invalid ZIP file boundary");
    std::string content(size, '\0');
    if (method == 0) {
      if (packed != size)
        throw std::runtime_error("Invalid stored ZIP size");
      content = data.substr(size_t(start), size);
    } else if (tangos_inflate(reinterpret_cast<const unsigned char *>(data.data() + start), packed,
                              reinterpret_cast<unsigned char *>(content.data()), size) != 0)
      throw std::runtime_error("Invalid or oversized ZIP compressed data");
    if (crc32(content) != crc)
      throw std::runtime_error("ZIP CRC mismatch: " + name);
    if (directory && size)
      throw std::runtime_error("ZIP directory contains file data");
    entries.push_back({name, std::move(content), directory});
    p = next;
  }
  if (p != end)
    throw std::runtime_error("ZIP directory length mismatch");
  // GitHub archives have a single enclosing directory. Strip it only when all entries share it.
  std::string prefix;
  if (!entries.empty()) {
    auto slash = entries.front().path.find('/');
    prefix = slash != std::string::npos  ? entries.front().path.substr(0, slash + 1)
             : entries.front().directory ? entries.front().path + "/"
                                         : "";
    for (auto &entry : entries)
      if (entry.path != prefix.substr(0, prefix.size() - 1) && entry.path.rfind(prefix, 0) != 0)
        prefix.clear();
  }
  bool descriptor = false;
  for (auto &entry : entries) {
    if (!prefix.empty()) {
      if (entry.directory && entry.path + "/" == prefix) {
        entry.path.clear();
        continue;
      }
      entry.path.erase(0, prefix.size());
    }
    auto why = blockedPath(entry.path, settings);
    if (!why.empty())
      throw std::runtime_error("Protected ZIP entry " + entry.path + ": " + why);
    if (!entry.directory && !(why = blockedBlob(entry.content)).empty())
      throw std::runtime_error("Protected ZIP content " + entry.path + ": " + why);
    if (entry.path == "tangos.json" && !entry.directory) {
      parseDescriptor(entry.content);
      descriptor = true;
    }
  }
  if (!descriptor)
    throw std::runtime_error("Project ZIP needs a valid root tangos.json");
  std::set<std::wstring, WindowsPathLess> files;
  for (auto &entry : entries) {
    if (entry.directory)
      continue;
    auto path = wide(entry.path);
    files.insert(path);
  }
  for (auto &entry : entries) {
    auto parent = fs::u8path(entry.path).parent_path();
    while (!parent.empty()) {
      auto path = parent.generic_wstring();
      if (files.count(path))
        throw std::runtime_error("ZIP file/directory collision: " + entry.path);
      parent = parent.parent_path();
    }
  }
  return entries;
}
Json summary(const std::vector<Entry> &entries) {
  Json files = Json::array();
  uint64_t bytes = 0;
  for (auto &entry : entries)
    if (!entry.directory) {
      files.push_back(entry.path);
      bytes += entry.content.size();
    }
  return {{"files", files}, {"bytes", bytes}};
}
} // namespace
Json inspectProjectZip(const fs::path &archive, const Settings &settings) {
  return summary(decode(archive, settings));
}
Json extractProjectZip(const fs::path &archive, const fs::path &destination,
                       const Settings &settings) {
  if (!destination.is_absolute() || fs::exists(destination))
    throw std::runtime_error("ZIP destination must be an absolute new folder");
  auto entries = decode(archive, settings);
  if (!fs::create_directory(destination))
    throw std::runtime_error("Cannot create ZIP destination");
  for (auto &entry : entries) {
    if (entry.path.empty())
      continue;
    auto path = confinedPath(destination, entry.path);
    if (entry.directory)
      fs::create_directories(path);
    else {
      fs::create_directories(path.parent_path());
      write(path, entry.content);
    }
  }
  return summary(entries);
}
} // namespace lite

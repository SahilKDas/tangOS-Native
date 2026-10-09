#include "images.h"
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
namespace lite {
namespace {
template <class T> struct Com {
  T *value = nullptr;
  ~Com() {
    if (value)
      value->Release();
  }
};
struct Apartment {
  HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~Apartment() {
    if (SUCCEEDED(status))
      CoUninitialize();
  }
};
} // namespace
std::string dibScreenshotBitmap(const std::string &dib) {
  BITMAPINFOHEADER header{};
  if (dib.size() < sizeof(header) || dib.size() > 160 * 1024 * 1024)
    throw std::runtime_error("Clipboard screenshot is truncated or exceeds 160 MiB");
  std::memcpy(&header, dib.data(), sizeof(header));
  auto height = std::abs(int64_t(header.biHeight));
  if (header.biSize < 40 || header.biSize > 124 || header.biSize > dib.size() ||
      header.biWidth <= 0 || header.biWidth > 8192 || !height || height > 8192 ||
      uint64_t(header.biWidth) * height > 40000000 || header.biPlanes != 1 ||
      (header.biBitCount != 1 && header.biBitCount != 4 && header.biBitCount != 8 &&
       header.biBitCount != 16 && header.biBitCount != 24 && header.biBitCount != 32) ||
      (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS &&
       header.biCompression != 6))
    throw std::runtime_error("Clipboard screenshot has unsupported or oversized bitmap dimensions");
  uint64_t palette = header.biClrUsed;
  if (header.biBitCount <= 8) {
    if (!palette)
      palette = uint64_t(1) << header.biBitCount;
    if (palette > (uint64_t(1) << header.biBitCount))
      throw std::runtime_error("Clipboard screenshot has an invalid color palette");
  } else if (palette > 256)
    throw std::runtime_error("Clipboard screenshot palette is too large");
  uint64_t masks = header.biSize == 40 ? (header.biCompression == BI_BITFIELDS ? 12
                                          : header.biCompression == 6          ? 16
                                                                               : 0)
                                       : 0;
  uint64_t offset = header.biSize + masks + palette * 4;
  uint64_t row = ((uint64_t(header.biWidth) * header.biBitCount + 31) / 32) * 4;
  uint64_t size = offset + row * height;
  if (size > dib.size() || size + sizeof(BITMAPFILEHEADER) > 160 * 1024 * 1024)
    throw std::runtime_error("Clipboard screenshot pixel data is truncated or exceeds 160 MiB");
  BITMAPFILEHEADER file{};
  file.bfType = 0x4d42;
  file.bfSize = DWORD(size + sizeof(file));
  file.bfOffBits = DWORD(offset + sizeof(file));
  std::string bitmap(reinterpret_cast<char *>(&file), sizeof(file));
  bitmap.append(dib.data(), size_t(size));
  if (header.biSize == sizeof(BITMAPV5HEADER)) {
    BITMAPV5HEADER info{};
    std::memcpy(&info, bitmap.data() + sizeof(file), sizeof(info));
    info.bV5ProfileData = info.bV5ProfileSize = 0;
    info.bV5CSType = 0x73524742;
    std::memcpy(bitmap.data() + sizeof(file), &info, sizeof(info));
  }
  return bitmap;
}
std::string dibScreenshotPng(const std::string &dib) {
  auto bitmap = dibScreenshotBitmap(dib);
  Apartment apartment;
  if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE)
    throw std::runtime_error("Cannot initialize screenshot compression");
  Com<IStream> input, output;
  Com<IWICImagingFactory> factory;
  Com<IWICBitmapDecoder> decoder;
  Com<IWICBitmapFrameDecode> source;
  Com<IWICFormatConverter> converter;
  Com<IWICBitmapEncoder> encoder;
  Com<IWICBitmapFrameEncode> frame;
  ULONG written = 0;
  LARGE_INTEGER start{};
  UINT width = 0, height = 0;
  auto fail = [] { throw std::runtime_error("Cannot compress clipboard screenshot as PNG"); };
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &input.value)) ||
      FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &output.value)) ||
      FAILED(input.value->Write(bitmap.data(), ULONG(bitmap.size()), &written)) ||
      written != bitmap.size() || FAILED(input.value->Seek(start, STREAM_SEEK_SET, nullptr)) ||
      FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory.value))) ||
      FAILED(factory.value->CreateDecoderFromStream(
          input.value, nullptr, WICDecodeMetadataCacheOnLoad, &decoder.value)) ||
      FAILED(decoder.value->GetFrame(0, &source.value)) ||
      FAILED(source.value->GetSize(&width, &height)) ||
      FAILED(factory.value->CreateFormatConverter(&converter.value)) ||
      FAILED(converter.value->Initialize(source.value, GUID_WICPixelFormat32bppBGRA,
                                         WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)) ||
      FAILED(factory.value->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder.value)) ||
      FAILED(encoder.value->Initialize(output.value, WICBitmapEncoderNoCache)) ||
      FAILED(encoder.value->CreateNewFrame(&frame.value, nullptr)) ||
      FAILED(frame.value->Initialize(nullptr)) || FAILED(frame.value->SetSize(width, height)))
    fail();
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (FAILED(frame.value->SetPixelFormat(&format)) ||
      !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) ||
      FAILED(frame.value->WriteSource(converter.value, nullptr)) || FAILED(frame.value->Commit()) ||
      FAILED(encoder.value->Commit()))
    fail();
  STATSTG info{};
  if (FAILED(output.value->Stat(&info, STATFLAG_NONAME)) || !info.cbSize.QuadPart ||
      info.cbSize.QuadPart > 16 * 1024 * 1024)
    throw std::runtime_error("Compressed clipboard screenshot exceeds 16 MiB");
  if (FAILED(output.value->Seek(start, STREAM_SEEK_SET, nullptr)))
    fail();
  std::string png(size_t(info.cbSize.QuadPart), '\0');
  ULONG read = 0;
  if (FAILED(output.value->Read(png.data(), ULONG(png.size()), &read)) || read != png.size())
    fail();
  return png;
}
ScreenshotInfo inspectScreenshot(const fs::path &path) {
  auto attributes = GetFileAttributesW(path.c_str());
  if (!path.is_absolute() || attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      !fs::is_regular_file(path))
    throw std::runtime_error("Choose a regular local screenshot file");
  auto extension = utf8(path.extension().wstring());
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".bmp" &&
      extension != ".gif")
    throw std::runtime_error("Screenshots must be PNG, JPEG, BMP or GIF");
  ScreenshotInfo info;
  info.bytes = fs::file_size(path);
  if (!info.bytes || info.bytes > 16 * 1024 * 1024)
    throw std::runtime_error("Each screenshot must be under 16 MiB");
  Apartment apartment;
  if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE)
    throw std::runtime_error("Cannot initialize the native image decoder");
  Com<IWICImagingFactory> factory;
  Com<IWICBitmapDecoder> decoder;
  Com<IWICBitmapFrameDecode> frame;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory.value))) ||
      FAILED(factory.value->CreateDecoderFromFilename(
          path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder.value)) ||
      FAILED(decoder.value->GetFrame(0, &frame.value)) ||
      FAILED(frame.value->GetSize(&info.width, &info.height)))
    throw std::runtime_error("The screenshot cannot be decoded as an image");
  GUID format{};
  if (FAILED(decoder.value->GetContainerFormat(&format)))
    throw std::runtime_error("Cannot identify the screenshot format");
  if (IsEqualGUID(format, GUID_ContainerFormatPng))
    info.format = "png";
  else if (IsEqualGUID(format, GUID_ContainerFormatJpeg))
    info.format = "jpg";
  else if (IsEqualGUID(format, GUID_ContainerFormatBmp))
    info.format = "bmp";
  else if (IsEqualGUID(format, GUID_ContainerFormatGif))
    info.format = "gif";
  else
    throw std::runtime_error("Unsupported screenshot image container");
  if ((extension == ".jpeg" ? "jpg" : extension.substr(1)) != info.format)
    throw std::runtime_error("Screenshot extension does not match its image container");
  if (!info.width || !info.height || info.width > 8192 || info.height > 8192 ||
      uint64_t(info.width) * info.height > 40000000)
    throw std::runtime_error("Screenshot dimensions exceed the 8192px / 40 megapixel limit");
  return info;
}
} // namespace lite

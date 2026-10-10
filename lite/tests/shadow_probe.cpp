#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
extern "C" void tangos_shadow(unsigned char *, unsigned, unsigned, unsigned, float, float, int,
                              float);
int main(int argc, char **argv) {
  if (argc != 8) {
    std::cerr << "shadow_probe WIDTH HEIGHT RADIUS BLUR OFFSET OPACITY OUTPUT.bmp\n";
    return 2;
  }
  try {
    unsigned w = std::stoul(argv[1]) + 80, h = std::stoul(argv[2]) + 80;
    if (w > 2048 || h > 2048 || w < 81 || h < 81)
      return 2;
    std::vector<unsigned char> pixels(size_t(w) * h * 4, 255);
    tangos_shadow(pixels.data(), w, h, 40, std::stof(argv[3]), std::stof(argv[4]),
                   std::stoi(argv[5]), std::stof(argv[6]));
    std::ofstream output(argv[7], std::ios::binary);
    auto u16 = [&](uint16_t value) { output.write(reinterpret_cast<char *>(&value), 2); };
    auto u32 = [&](uint32_t value) { output.write(reinterpret_cast<char *>(&value), 4); };
    u16(0x4d42);
    u32(54 + uint32_t(pixels.size()));
    u32(0); u32(54); u32(40); u32(w); u32(uint32_t(-int32_t(h)));
    u16(1); u16(32); u32(0); u32(uint32_t(pixels.size()));
    u32(0); u32(0); u32(0); u32(0);
    output.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    return output ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

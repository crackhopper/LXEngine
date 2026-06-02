#include "infra/image/rgba_image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include <tinyexr/tinyexr.h>

namespace LX_infra::image {

void writeRgba32fExr(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image) {
  const char *error = nullptr;
  constexpr int saveAsHalf = 1;
  const int result = SaveEXR(image.rgba.data(), static_cast<int>(image.width),
                             static_cast<int>(image.height), 4, saveAsHalf,
                             path.string().c_str(), &error);
  if (result != TINYEXR_SUCCESS) {
    std::string message = "failed to write offline EXR " + path.string();
    if (error != nullptr) {
      message += ": ";
      message += error;
      FreeEXRErrorMessage(error);
    }
    throw std::runtime_error(message);
  }
}

void writeToneMappedPng(const std::filesystem::path &path,
                        const LX_core::offline::OfflineReadbackImage &image,
                        const LX_core::image::ToneMappingSettings &settings) {
  std::vector<unsigned char> pixels(image.pixelCount() * 4);
  for (usize i = 0; i < image.pixelCount(); ++i) {
    pixels[i * 4 + 0] =
        LX_core::image::toneMapLinearToSrgb8(image.rgba[i * 4 + 0], settings);
    pixels[i * 4 + 1] =
        LX_core::image::toneMapLinearToSrgb8(image.rgba[i * 4 + 1], settings);
    pixels[i * 4 + 2] =
        LX_core::image::toneMapLinearToSrgb8(image.rgba[i * 4 + 2], settings);
    const float sourceAlpha = image.rgba[i * 4 + 3];
    const float alpha =
        std::isfinite(sourceAlpha) ? std::clamp(sourceAlpha, 0.0f, 1.0f) : 0.0f;
    pixels[i * 4 + 3] = static_cast<unsigned char>(std::round(alpha * 255.0f));
  }
  const int ok = stbi_write_png(path.string().c_str(), static_cast<int>(image.width),
                                static_cast<int>(image.height), 4, pixels.data(),
                                static_cast<int>(image.width * 4));
  if (ok == 0) {
    throw std::runtime_error("failed to write offline PNG " + path.string());
  }
}

void writeRawRgba32f(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image) {
  std::ofstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open offline RGBA32F dump " + path.string());
  }
  std::vector<char> bytes(image.rgba.size() * sizeof(float));
  std::memcpy(bytes.data(), image.rgba.data(), bytes.size());
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace LX_infra::image

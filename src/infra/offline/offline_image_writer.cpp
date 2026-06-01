#include "infra/offline/offline_image_writer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include <tinyexr/tinyexr.h>

namespace LX_infra::offline {
namespace {

[[nodiscard]] std::string jsonEscape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += ch;
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string modeName(OfflineToneMappingMode mode) {
  switch (mode) {
  case OfflineToneMappingMode::Aces:
    return "aces";
  case OfflineToneMappingMode::Reinhard:
    return "reinhard";
  }
  return "aces";
}

[[nodiscard]] float applyToneMap(float value, const OfflineToneMappingSettings &settings) {
  const float exposed = std::max(0.0f, value * std::max(0.0f, settings.exposure));
  if (settings.mode == OfflineToneMappingMode::Reinhard) {
    return exposed / (exposed + 1.0f);
  }
  constexpr float a = 2.51f;
  constexpr float b = 0.03f;
  constexpr float c = 2.43f;
  constexpr float d = 0.59f;
  constexpr float e = 0.14f;
  return std::clamp((exposed * (a * exposed + b)) /
                        (exposed * (c * exposed + d) + e),
                    0.0f, 1.0f);
}

[[nodiscard]] std::filesystem::path makeDefaultBasePath(
    const OfflineImageOutputRequest &request) {
  const std::string sceneName =
      request.job.scene.name.empty() ? "scene" : request.job.scene.name;
  const std::string profileName =
      request.profileName.empty() ? "profile" : request.profileName;
  return std::filesystem::path("artifacts") / "offline" / sceneName /
         profileName / "render";
}

[[nodiscard]] std::filesystem::path resolveBasePath(
    const OfflineImageOutputRequest &request) {
  std::filesystem::path out = request.job.outputPath;
  if (out.empty()) {
    return makeDefaultBasePath(request);
  }
  if (std::filesystem::exists(out) && std::filesystem::is_directory(out)) {
    return out / "render";
  }
  const std::string extension = out.extension().string();
  if (extension == ".exr" || extension == ".png" || extension == ".json" ||
      extension == ".rgba32f") {
    out.replace_extension();
  }
  return out;
}

void validateImage(const LX_core::offline::OfflineReadbackImage &image) {
  if (image.width == 0 || image.height == 0) {
    throw std::runtime_error("offline image output requires non-zero dimensions");
  }
  const usize expected = image.pixelCount() * 4;
  if (image.rgba.size() != expected) {
    throw std::runtime_error("offline image output expected RGBA32F buffer size " +
                             std::to_string(expected) + ", got " +
                             std::to_string(image.rgba.size()));
  }
}

void writeExr(const std::filesystem::path &path,
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

void writePng(const std::filesystem::path &path,
              const LX_core::offline::OfflineReadbackImage &image,
              const OfflineToneMappingSettings &settings) {
  std::vector<unsigned char> pixels(image.pixelCount() * 4);
  for (usize i = 0; i < image.pixelCount(); ++i) {
    pixels[i * 4 + 0] = toneMapLinearToSrgb8(image.rgba[i * 4 + 0], settings);
    pixels[i * 4 + 1] = toneMapLinearToSrgb8(image.rgba[i * 4 + 1], settings);
    pixels[i * 4 + 2] = toneMapLinearToSrgb8(image.rgba[i * 4 + 2], settings);
    const float alpha = std::clamp(image.rgba[i * 4 + 3], 0.0f, 1.0f);
    pixels[i * 4 + 3] = static_cast<unsigned char>(std::round(alpha * 255.0f));
  }
  const int ok = stbi_write_png(path.string().c_str(), static_cast<int>(image.width),
                                static_cast<int>(image.height), 4, pixels.data(),
                                static_cast<int>(image.width * 4));
  if (ok == 0) {
    throw std::runtime_error("failed to write offline PNG " + path.string());
  }
}

void writeRaw(const std::filesystem::path &path,
              const LX_core::offline::OfflineReadbackImage &image) {
  std::ofstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open offline RGBA32F dump " + path.string());
  }
  stream.write(reinterpret_cast<const char *>(image.rgba.data()),
               static_cast<std::streamsize>(image.rgba.size() * sizeof(float)));
}

void writeMetadata(const std::filesystem::path &path,
                   const OfflineImageOutputRequest &request,
                   const OfflineImageOutputResult &result) {
  std::ofstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open offline metadata " + path.string());
  }
  stream << "{\n";
  stream << "  \"scenePath\": \"" << jsonEscape(request.scenePath.string()) << "\",\n";
  stream << "  \"sceneName\": \"" << jsonEscape(request.job.scene.name) << "\",\n";
  stream << "  \"cameraPath\": \"" << jsonEscape(request.job.cameraPath) << "\",\n";
  stream << "  \"profile\": \"" << jsonEscape(request.profileName) << "\",\n";
  stream << "  \"width\": " << request.image.width << ",\n";
  stream << "  \"height\": " << request.image.height << ",\n";
  stream << "  \"samples\": " << request.job.profile.samples << ",\n";
  stream << "  \"maxDepth\": " << request.job.profile.maxDepth << ",\n";
  stream << "  \"seed\": " << request.job.profile.seed << ",\n";
  stream << "  \"outputFormat\": \"" << jsonEscape(request.job.profile.outputFormat) << "\",\n";
  stream << "  \"exrStorage\": \"rgba-half-scene-linear\",\n";
  stream << "  \"pngPreview\": {\n";
  stream << "    \"toneMapping\": \"" << modeName(request.toneMapping.mode) << "\",\n";
  stream << "    \"exposure\": " << request.toneMapping.exposure << ",\n";
  stream << "    \"gamma\": " << request.toneMapping.gamma << "\n";
  stream << "  },\n";
  stream << "  \"git\": {\n";
  stream << "    \"commit\": \"" << jsonEscape(request.gitCommit) << "\",\n";
  stream << "    \"dirty\": " << (request.gitDirty ? "true" : "false") << "\n";
  stream << "  },\n";
  stream << "  \"files\": {\n";
  stream << "    \"exr\": \"" << jsonEscape(result.exrPath.string()) << "\",\n";
  stream << "    \"png\": \"" << jsonEscape(result.pngPath.string()) << "\",\n";
  stream << "    \"rawRgba32f\": \"" << jsonEscape(result.rawPath.string()) << "\"\n";
  stream << "  }\n";
  stream << "}\n";
}

} // namespace

unsigned char toneMapLinearToSrgb8(
    float value, const OfflineToneMappingSettings &settings) {
  const float mapped = applyToneMap(value, settings);
  const float gamma = std::max(settings.gamma, 0.0001f);
  const float srgb = std::pow(std::clamp(mapped, 0.0f, 1.0f), 1.0f / gamma);
  return static_cast<unsigned char>(
      std::round(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
}

OfflineImageOutputResult
writeOfflineImageOutputs(const OfflineImageOutputRequest &request) {
  validateImage(request.image);
  const std::filesystem::path basePath = resolveBasePath(request);
  if (const auto parent = basePath.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  OfflineImageOutputResult result;
  result.exrPath = basePath;
  result.exrPath.replace_extension(".exr");
  result.pngPath = basePath;
  result.pngPath.replace_extension(".png");
  result.metadataPath = basePath;
  result.metadataPath.replace_extension(".json");
  result.rawPath = basePath;
  result.rawPath.replace_extension(".rgba32f");

  writeExr(result.exrPath, request.image);
  writePng(result.pngPath, request.image, request.toneMapping);
  if (request.writeRawRgba32f) {
    writeRaw(result.rawPath, request.image);
  } else {
    result.rawPath.clear();
  }
  writeMetadata(result.metadataPath, request, result);
  return result;
}

} // namespace LX_infra::offline

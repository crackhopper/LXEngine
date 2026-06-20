#include "infra/offline/offline_image_writer.hpp"

#include "infra/image/rgba_image_io.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <cmath>
#include <sstream>
#include <stdexcept>

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

float halfToFloat(u16 value) {
  const u16 sign = static_cast<u16>((value >> 15u) & 0x1u);
  const u16 exponent =
      static_cast<u16>((value >> 10u) & 0x1fu);
  const u16 mantissa = static_cast<u16>(value & 0x03ffu);
  const float signScale = sign == 0 ? 1.0f : -1.0f;
  if (exponent == 0) {
    if (mantissa == 0) {
      return signScale * 0.0f;
    }
    return signScale * std::ldexp(static_cast<float>(mantissa), -24);
  }
  if (exponent == 31) {
    return mantissa == 0 ? signScale * std::numeric_limits<float>::infinity()
                         : std::numeric_limits<float>::quiet_NaN();
  }
  return signScale * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                static_cast<int>(exponent) - 15);
}

[[nodiscard]] std::filesystem::path makeDefaultBasePath(
    const OfflineImageOutputRequest &request) {
  const std::filesystem::path outDir =
      request.output.outDir.empty()
          ? std::filesystem::path("artifacts")
          : request.output.outDir;
  return outDir / "render";
}

[[nodiscard]] std::filesystem::path resolveBasePath(
    const OfflineImageOutputRequest &request) {
  std::filesystem::path out = request.outputPath;
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

[[nodiscard]] std::string sceneNameFromPath(const std::filesystem::path &path) {
  const std::string stem = path.stem().string();
  return stem.empty() ? "unknown" : stem;
}

void writeMetadata(const std::filesystem::path &path,
                   const OfflineImageOutputRequest &request,
                   const LX_core::offline::OfflineReadbackImage &image,
                   const OfflineImageOutputResult &result) {
  std::ofstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open offline metadata " + path.string());
  }
  stream << "{\n";
  stream << "  \"scenePath\": \"" << jsonEscape(request.scenePath.string()) << "\",\n";
  stream << "  \"sceneName\": \"" << jsonEscape(sceneNameFromPath(request.scenePath)) << "\",\n";
  stream << "  \"cameraPath\": \""
         << jsonEscape(request.output.cameraPath) << "\",\n";
  stream << "  \"profile\": \""
         << jsonEscape(request.profileName) << "\",\n";
  stream << "  \"width\": " << image.width << ",\n";
  stream << "  \"height\": " << image.height << ",\n";
  stream << "  \"samples\": " << request.offline.samples << ",\n";
  stream << "  \"maxBounce\": " << request.offline.maxBounce << ",\n";
  stream << "  \"seed\": " << request.offline.seed << ",\n";
  stream << "  \"readback\": {\n";
  stream << "    \"name\": \"" << jsonEscape(request.payload.name) << "\",\n";
  stream << "    \"target\": \"" << jsonEscape(request.payload.target) << "\",\n";
  stream << "    \"format\": \"" << jsonEscape(request.payload.format) << "\",\n";
  stream << "    \"mediaType\": \"" << jsonEscape(request.payload.mediaType) << "\"\n";
  stream << "  },\n";
  stream << "  \"outputFormat\": \""
         << jsonEscape(request.output.outputFormat) << "\",\n";
  stream << "  \"exrStorage\": \"rgba-half-scene-linear\",\n";
  stream << "  \"pngPreview\": {\n";
  stream << "    \"toneMapping\": \""
         << LX_core::image::toneMappingModeName(request.toneMapping.mode) << "\",\n";
  stream << "    \"exposure\": " << request.toneMapping.exposure << ",\n";
  stream << "    \"gamma\": " << request.toneMapping.gamma << "\n";
  stream << "  },\n";
  stream << "  \"buildInfo\": \"" << jsonEscape(request.buildInfo) << "\",\n";
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
  return LX_core::image::toneMapLinearToSrgb8(value, settings);
}

OfflineImageOutputResult
writeOfflineImageOutputs(const OfflineImageOutputRequest &request) {
  const LX_core::offline::OfflineReadbackImage image =
      offlineImageFromPayload(request.payload);
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

  LX_infra::image::writeRgba32fExr(result.exrPath, image);
  LX_infra::image::writeToneMappedPng(result.pngPath, image,
                                      request.toneMapping);
  if (request.writeRawRgba32f) {
    LX_infra::image::writeRawRgba32f(result.rawPath, image);
  } else {
    result.rawPath.clear();
  }
  writeMetadata(result.metadataPath, request, image, result);
  return result;
}

LX_core::offline::OfflineReadbackImage
offlineImageFromPayload(const LX_core::FrameGraphExecutionPayload &payload) {
  if (payload.format != "RGBA32Float" && payload.format != "RGBA16Float") {
    throw std::runtime_error(
        "offline image output expected RGBA32Float or RGBA16Float payload");
  }
  if (payload.kind != LX_core::RenderPathOutputKind::Image2D) {
    throw std::runtime_error("offline image output expected image2d payload");
  }
  if (payload.mediaType != "application/x-lxe-rgba32f-image2d" &&
      payload.mediaType != "application/x-lxe-rgba16f-image2d") {
    throw std::runtime_error(
        "offline image output expected RGBA float image2d payload");
  }
  if (payload.extent.x == 0 || payload.extent.y == 0) {
    throw std::runtime_error("offline image output requires non-zero dimensions");
  }

  const usize pixelCount =
      static_cast<usize>(payload.extent.x) * static_cast<usize>(payload.extent.y);
  const usize expectedBytes =
      payload.format == "RGBA32Float" ? pixelCount * 4u * sizeof(float)
                                      : pixelCount * 4u * sizeof(u16);
  if (payload.bytes.size() != expectedBytes) {
    throw std::runtime_error("offline image output expected RGBA32F byte size " +
                             std::to_string(expectedBytes) + ", got " +
                             std::to_string(payload.bytes.size()));
  }

  LX_core::offline::OfflineReadbackImage image;
  image.width = payload.extent.x;
  image.height = payload.extent.y;
  image.rgba.resize(pixelCount * 4u);
  if (payload.format == "RGBA32Float") {
    std::memcpy(image.rgba.data(), payload.bytes.data(), payload.bytes.size());
  } else {
    const auto *halfPixels =
        reinterpret_cast<const u16 *>(payload.bytes.data());
    for (usize i = 0; i < image.rgba.size(); ++i) {
      image.rgba[i] = halfToFloat(halfPixels[i]);
    }
  }
  return image;
}

} // namespace LX_infra::offline

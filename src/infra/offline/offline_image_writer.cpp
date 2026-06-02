#include "infra/offline/offline_image_writer.hpp"

#include "infra/image/rgba_image_io.hpp"

#include <filesystem>
#include <fstream>
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

[[nodiscard]] std::filesystem::path makeDefaultBasePath(
    const OfflineImageOutputRequest &request) {
  const std::filesystem::path outDir =
      request.job.output.outDir.empty()
          ? std::filesystem::path("artifacts")
          : request.job.output.outDir;
  return outDir / "render";
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
  stream << "  \"cameraPath\": \""
         << jsonEscape(request.job.output.cameraPath) << "\",\n";
  stream << "  \"profile\": \""
         << jsonEscape(request.job.profileName) << "\",\n";
  stream << "  \"width\": " << request.image.width << ",\n";
  stream << "  \"height\": " << request.image.height << ",\n";
  stream << "  \"samples\": " << request.job.offline.samples << ",\n";
  stream << "  \"maxBounce\": " << request.job.offline.maxBounce << ",\n";
  stream << "  \"seed\": " << request.job.offline.seed << ",\n";
  stream << "  \"outputFormat\": \""
         << jsonEscape(request.job.output.outputFormat) << "\",\n";
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

  LX_infra::image::writeRgba32fExr(result.exrPath, request.image);
  LX_infra::image::writeToneMappedPng(result.pngPath, request.image,
                                      request.toneMapping);
  if (request.writeRawRgba32f) {
    LX_infra::image::writeRawRgba32f(result.rawPath, request.image);
  } else {
    result.rawPath.clear();
  }
  writeMetadata(result.metadataPath, request, result);
  return result;
}

} // namespace LX_infra::offline

#include "infra/offline/offline_image_writer.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] bool hasPrefix(const std::filesystem::path &path,
                             const std::string &prefix) {
  std::ifstream stream(path, std::ios::binary);
  std::string data(prefix.size(), '\0');
  stream.read(data.data(), static_cast<std::streamsize>(data.size()));
  return data == prefix;
}

[[nodiscard]] std::filesystem::path makeTempDir() {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "lxe_offline_image_writer_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void testToneMappingMatchesAcesPreview() {
  LX_infra::offline::OfflineToneMappingSettings settings;
  settings.exposure = 1.0f;
  settings.gamma = 2.2f;
  settings.mode = LX_infra::offline::OfflineToneMappingMode::Aces;

  const unsigned char black =
      LX_infra::offline::toneMapLinearToSrgb8(0.0f, settings);
  const unsigned char white =
      LX_infra::offline::toneMapLinearToSrgb8(1.0f, settings);

  EXPECT(black == 0, "black stays black");
  EXPECT(white > 220 && white < 240, "ACES + gamma maps 1.0 near display white");
}

void testWritesExrPngJsonAndRaw() {
  const std::filesystem::path dir = makeTempDir();
  LX_infra::offline::OfflineImageOutputRequest request;
  request.scenePath = "assets/scenes/ibl_metal_sphere.scene.yaml";
  request.buildInfo = "test-binary 0.1.0-dev (test-dirty, Debug, Linux-x86_64)";
  request.job.outputPath = dir / "beauty";
  request.job.scene.name = "writer_test";
  request.job.profileName = "mvp";
  request.job.output.cameraPath = "/game_cam";
  request.job.output.width = 2;
  request.job.output.height = 2;
  request.job.offline.samples = 4;
  request.job.offline.maxBounce = 2;
  request.job.offline.seed = 9;
  request.image.width = 2;
  request.image.height = 2;
  request.image.rgba = {
      0.0f, 0.0f, 0.0f, 1.0f,
      1.0f, 0.5f, 0.25f, 1.0f,
      4.0f, 2.0f, 1.0f, 1.0f,
      0.1f, 0.2f, 0.3f, 1.0f,
  };

  const auto result = LX_infra::offline::writeOfflineImageOutputs(request);

  EXPECT(std::filesystem::exists(result.exrPath), "EXR file should exist");
  EXPECT(std::filesystem::exists(result.pngPath), "PNG file should exist");
  EXPECT(std::filesystem::exists(result.metadataPath), "metadata should exist");
  EXPECT(std::filesystem::exists(result.rawPath), "raw RGBA32F should exist");
  EXPECT(hasPrefix(result.exrPath, "\x76\x2f\x31\x01"), "EXR magic should match");
  EXPECT(hasPrefix(result.pngPath, "\x89PNG\r\n\x1a\n"), "PNG signature should match");
  EXPECT(std::filesystem::file_size(result.rawPath) == 2u * 2u * 4u * sizeof(float),
         "raw dump size should match RGBA32F");

  std::ifstream metadata(result.metadataPath);
  const std::string text((std::istreambuf_iterator<char>(metadata)),
                         std::istreambuf_iterator<char>());
  EXPECT(text.find("\"exrStorage\": \"rgba-half-scene-linear\"") !=
             std::string::npos,
         "metadata should describe EXR storage");
  EXPECT(text.find("\"toneMapping\": \"aces\"") != std::string::npos,
         "metadata should describe PNG tone mapping");
  EXPECT(text.find("\"profile\": \"mvp\"") != std::string::npos,
         "metadata should record job profile name");
  EXPECT(text.find("\"buildInfo\": \"test-binary 0.1.0-dev") !=
             std::string::npos,
         "metadata should record composed build info string");
}

void testWritesPngWithNanAlpha() {
  const std::filesystem::path dir = makeTempDir() / "nan_alpha";
  LX_infra::offline::OfflineImageOutputRequest request;
  request.job.outputPath = dir / "beauty";
  request.job.output.width = 1;
  request.job.output.height = 1;
  request.image.width = 1;
  request.image.height = 1;
  request.image.rgba = {
      0.25f, 0.5f, 1.0f, std::numeric_limits<float>::quiet_NaN(),
  };

  const auto result = LX_infra::offline::writeOfflineImageOutputs(request);
  EXPECT(std::filesystem::exists(result.pngPath),
         "NaN alpha image should still write PNG");
}

void testDirectoryOutUsesRenderBasename() {
  const std::filesystem::path dir = makeTempDir() / "nested";
  std::filesystem::create_directories(dir);
  LX_infra::offline::OfflineImageOutputRequest request;
  request.job.outputPath = dir;
  request.job.output.width = 1;
  request.job.output.height = 1;
  request.image.width = 1;
  request.image.height = 1;
  request.image.rgba = {0.0f, 0.0f, 0.0f, 1.0f};

  const auto result = LX_infra::offline::writeOfflineImageOutputs(request);
  EXPECT(result.exrPath.filename() == "render.exr",
         "directory output uses render.exr");
  EXPECT(result.pngPath.filename() == "render.png",
         "directory output uses render.png");
}

void testDefaultOutUsesOutputProfileOutDir() {
  const std::filesystem::path outDir = makeTempDir() / "profile_out";
  LX_infra::offline::OfflineImageOutputRequest request;
  request.job.scene.name = "writer_test";
  request.job.profileName = "mvp";
  request.job.output.outDir = outDir;
  request.job.output.width = 1;
  request.job.output.height = 1;
  request.image.width = 1;
  request.image.height = 1;
  request.image.rgba = {0.0f, 0.0f, 0.0f, 1.0f};

  const auto result = LX_infra::offline::writeOfflineImageOutputs(request);
  EXPECT(result.exrPath == outDir / "render.exr",
         "default output path should use output profile outDir");
}

void testExplicitOutputPathOverridesOutputProfileOutDir() {
  const std::filesystem::path dir = makeTempDir() / "override";
  LX_infra::offline::OfflineImageOutputRequest request;
  request.job.outputPath = dir / "beauty.png";
  request.job.output.outDir = "artifacts/offline/mvp";
  request.job.output.width = 1;
  request.job.output.height = 1;
  request.image.width = 1;
  request.image.height = 1;
  request.image.rgba = {0.0f, 0.0f, 0.0f, 1.0f};

  const auto result = LX_infra::offline::writeOfflineImageOutputs(request);
  EXPECT(result.exrPath == dir / "beauty.exr",
         "explicit output path should override output profile outDir");
}

} // namespace

int main() {
  testToneMappingMatchesAcesPreview();
  testWritesExrPngJsonAndRaw();
  testWritesPngWithNanAlpha();
  testDirectoryOutUsesRenderBasename();
  testDefaultOutUsesOutputProfileOutDir();
  testExplicitOutputPathOverridesOutputProfileOutDir();
  if (failures != 0) {
    std::cerr << "test_offline_image_writer failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_offline_image_writer passed\n";
  return 0;
}

#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_scene_compiler.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct CliArgs final {
  std::filesystem::path scenePath;
  std::string cameraPath;
  LX_core::offline::OfflineRenderCliOverrides overrides;
};

[[nodiscard]] u32 parseU32(const std::string &value, const char *name) {
  const unsigned long parsed = std::stoul(value);
  if (parsed == 0 || parsed > 0xfffffffful) {
    throw std::runtime_error(std::string(name) + " must be a positive u32");
  }
  return static_cast<u32>(parsed);
}

[[nodiscard]] CliArgs parseArgs(int argc, char **argv) {
  CliArgs args;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    auto requireValue = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("missing value for ") + name);
      }
      return argv[++i];
    };
    if (key == "--scene") {
      args.scenePath = requireValue("--scene");
    } else if (key == "--camera") {
      args.cameraPath = requireValue("--camera");
    } else if (key == "--profile") {
      args.overrides.profileName = requireValue("--profile");
    } else if (key == "--width") {
      args.overrides.width = parseU32(requireValue("--width"), "--width");
    } else if (key == "--height") {
      args.overrides.height = parseU32(requireValue("--height"), "--height");
    } else if (key == "--samples") {
      args.overrides.samples = parseU32(requireValue("--samples"), "--samples");
    } else if (key == "--seed") {
      args.overrides.seed = parseU32(requireValue("--seed"), "--seed");
    } else if (key == "--out") {
      args.overrides.outputPath = requireValue("--out");
    } else if (key == "--help" || key == "-h") {
      throw std::runtime_error(
          "usage: lxe_offline_render --scene SCENE [--camera PATH] "
          "[--profile NAME] [--width N] [--height N] [--samples N] "
          "[--seed N] [--out PATH]");
    } else {
      throw std::runtime_error("unknown argument: " + key);
    }
  }
  if (args.scenePath.empty()) {
    throw std::runtime_error("--scene is required");
  }
  return args;
}

void writeDebugDump(const std::filesystem::path &outPath,
                    const LX_core::offline::OfflineReadbackImage &image) {
  if (outPath.empty()) {
    return;
  }
  std::filesystem::path path = outPath;
  if (path.extension().empty()) {
    path += ".rgba32f";
  }
  if (const auto parent = path.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open offline debug dump: " +
                             path.string());
  }
  stream.write(reinterpret_cast<const char *>(image.rgba.data()),
               static_cast<std::streamsize>(image.rgba.size() * sizeof(float)));
}

} // namespace

int main(int argc, char **argv) {
  try {
    const CliArgs args = parseArgs(argc, argv);
    const auto document = LX_infra::scene_io::loadSceneDocument(args.scenePath);
    LX_core::offline::OfflineRenderProfiles profiles =
        document.hasOfflineRenderProfiles()
            ? document.offlineRenderProfiles()
            : LX_core::offline::makeDefaultOfflineRenderProfiles();
    auto resolved =
        LX_core::offline::resolveOfflineRenderProfile(profiles, args.overrides);

    LX_infra::offline::OfflineAssetResolver resolver(args.scenePath);
    LX_infra::offline::OfflineSceneCompiler compiler(resolver);
    auto scene = compiler.compile(document, args.cameraPath);
    for (const auto &warning : scene.warnings) {
      std::cerr << "[offline warning] " << warning << '\n';
    }

    LX_core::offline::OfflineRenderJob job;
    job.scene = std::move(scene);
    job.profile = resolved.profile;
    job.outputPath = resolved.outputPath.value_or("");
    job.cameraPath = args.cameraPath;

    LX_core::backend::offline::VulkanOfflineRenderer renderer;
    const auto image = renderer.render(job);
    if (image.rgba.empty()) {
      throw std::runtime_error("offline render readback was empty");
    }
    for (const float value : image.rgba) {
      if (!std::isfinite(value)) {
        throw std::runtime_error("offline render readback contained non-finite values");
      }
    }
    writeDebugDump(job.outputPath, image);
    const usize center =
        (static_cast<usize>(image.height / 2) * image.width + image.width / 2) *
        4;
    std::cout << "lxe_offline_render completed " << image.width << "x"
              << image.height << " samples=" << job.profile.samples
              << " center=(" << image.rgba[center + 0] << ", "
              << image.rgba[center + 1] << ", " << image.rgba[center + 2]
              << ")\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "lxe_offline_render error: " << error.what() << '\n';
    return 1;
  }
}

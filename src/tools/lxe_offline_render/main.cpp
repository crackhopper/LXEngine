#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "infra/build_info/build_info.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/offline/offline_image_writer.hpp"
#include "infra/offline/offline_scene_loader.hpp"
#include "infra/scene_io/scene_document.hpp"
#include "tools/lxe_offline_render/offline_render_cli.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] constexpr const char *usageText() {
  return "usage: lxe_offline_render --scene SCENE "
         "[--profile NAME] [--width N] [--height N] [--samples N] "
         "[--max-bounce N] [--seed N] [--out PATH] [--version]";
}

[[nodiscard]] bool hasHelpArg(const std::vector<std::string> &args) {
  return std::find(args.begin(), args.end(), "--help") != args.end() ||
         std::find(args.begin(), args.end(), "-h") != args.end();
}

[[nodiscard]] std::vector<std::string> collectArgs(int argc, char **argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<usize>(std::max(argc - 1, 0)));
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return args;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const std::vector<std::string> rawArgs = collectArgs(argc, argv);
    if (hasHelpArg(rawArgs)) {
      std::cout << usageText() << '\n';
      return 0;
    }
    const std::string buildInfo =
        LX_infra::currentBuildInfoString("lxe_offline_render");
    if (std::find(rawArgs.begin(), rawArgs.end(), "--version") !=
        rawArgs.end()) {
      std::cout << buildInfo << '\n';
      return 0;
    }
    const auto args =
        LX_tools::offline_render::parseOfflineRenderCliArguments(rawArgs);
    const auto document = LX_infra::scene_io::loadSceneDocument(args.scenePath);
    const auto profiles =
        document.hasRenderProfileDocument()
            ? document.renderProfileDocument()
            : LX_core::offline::makeDefaultRenderProfileDocument();
    const auto resolved =
        LX_core::offline::resolveRenderProfileDocument(profiles, args.overrides);

    LX_infra::offline::OfflineAssetResolver resolver(args.scenePath);
    LX_infra::offline::OfflineSceneLoader loader(resolver);
    auto loaded = loader.load(document, resolved.output.cameraPath);
    for (const auto &warning : loaded.warnings) {
      std::cerr << "[offline warning] " << warning << '\n';
    }

    LX_core::backend::offline::VulkanOfflineRenderer renderer;
    LX_core::backend::offline::VulkanOfflineRenderRequest renderRequest;
    renderRequest.scene = std::move(loaded.table);
    renderRequest.renderSettings = loaded.renderSettings;
    renderRequest.output = resolved.output;
    renderRequest.offline = resolved.offline;
    renderRequest.profileName = resolved.profileName;
    renderRequest.outputPath = resolved.outputPath.value_or("");
    renderRequest.renderPathGraphUri = resolved.output.renderPathGraph;
    LX_core::offline::validateOfflineRenderInputs(renderRequest.scene,
                                                  renderRequest.output);
    const auto renderResult = renderer.render(std::move(renderRequest));
    const auto &image = renderResult.image;
    if (image.rgba.empty()) {
      throw std::runtime_error("offline render readback was empty");
    }
    for (const float value : image.rgba) {
      if (!std::isfinite(value)) {
        throw std::runtime_error("offline render readback contained non-finite values");
      }
    }
    LX_infra::offline::OfflineImageOutputRequest outputRequest;
    outputRequest.output = resolved.output;
    outputRequest.offline = resolved.offline;
    outputRequest.profileName = resolved.profileName;
    outputRequest.outputPath = resolved.outputPath.value_or("");
    outputRequest.payload = renderResult.payload;
    outputRequest.scenePath = args.scenePath;
    outputRequest.buildInfo = buildInfo;
    const auto outputs =
        LX_infra::offline::writeOfflineImageOutputs(outputRequest);
    const usize center =
        (static_cast<usize>(image.height / 2) * image.width + image.width / 2) *
        4;
    std::cout << "lxe_offline_render completed " << image.width << "x"
              << image.height << " samples=" << resolved.offline.samples
              << " center=(" << image.rgba[center + 0] << ", "
              << image.rgba[center + 1] << ", " << image.rgba[center + 2]
              << ") exr=" << outputs.exrPath.string()
              << " png=" << outputs.pngPath.string() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "lxe_offline_render error: " << error.what() << '\n';
    return 1;
  }
}

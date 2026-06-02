#include "tools/lxe_offline_render/offline_render_cli.hpp"

#include "infra/scene_io/scene_document.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace LX_tools::offline_render {
namespace {

[[nodiscard]] bool isOption(const std::string &value) {
  return value.size() > 2 && value[0] == '-' && value[1] == '-';
}

[[nodiscard]] std::string requireValue(const std::vector<std::string> &args,
                                       usize &index,
                                       const std::string &option) {
  if (index + 1 >= args.size() || isOption(args[index + 1])) {
    throw std::runtime_error(option + " requires a value");
  }
  ++index;
  return args[index];
}

[[nodiscard]] u32 parsePositiveU32(const std::string &text,
                                   const std::string &option) {
  usize parsed = 0;
  const unsigned long value = std::stoul(text, &parsed, 10);
  if (parsed != text.size() || value == 0 ||
      value > std::numeric_limits<u32>::max()) {
    throw std::runtime_error(option + " must be a positive integer");
  }
  return static_cast<u32>(value);
}

} // namespace

OfflineRenderCliOptions
parseOfflineRenderCliArguments(const std::vector<std::string> &args) {
  OfflineRenderCliOptions options;
  for (usize i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--scene") {
      options.scenePath = requireValue(args, i, arg);
    } else if (arg == "--camera") {
      throw std::runtime_error(
          "--camera was removed; select the camera through --profile");
    } else if (arg == "--profile") {
      options.overrides.profileName = requireValue(args, i, arg);
    } else if (arg == "--width") {
      options.overrides.width =
          parsePositiveU32(requireValue(args, i, arg), arg);
    } else if (arg == "--height") {
      options.overrides.height =
          parsePositiveU32(requireValue(args, i, arg), arg);
    } else if (arg == "--samples") {
      options.overrides.samples =
          parsePositiveU32(requireValue(args, i, arg), arg);
    } else if (arg == "--max-bounce") {
      options.overrides.maxBounce =
          parsePositiveU32(requireValue(args, i, arg), arg);
    } else if (arg == "--max-depth") {
      throw std::runtime_error(
          "--max-depth was removed; use --max-bounce instead");
    } else if (arg == "--seed") {
      options.overrides.seed =
          parsePositiveU32(requireValue(args, i, arg), arg);
    } else if (arg == "--out") {
      options.overrides.outputPath = requireValue(args, i, arg);
    } else {
      throw std::runtime_error("unknown lxe_offline_render option: " + arg);
    }
  }
  if (options.scenePath.empty()) {
    throw std::runtime_error("lxe_offline_render requires --scene");
  }
  return options;
}

LX_core::offline::ResolvedRenderProfile
loadResolvedRenderProfile(const OfflineRenderCliOptions &options) {
  const LX_infra::scene_io::SceneDocument document =
      LX_infra::scene_io::loadSceneDocument(options.scenePath);
  const LX_core::offline::RenderProfileDocument profiles =
      document.hasRenderProfileDocument()
          ? document.renderProfileDocument()
          : LX_core::offline::makeDefaultRenderProfileDocument();
  return LX_core::offline::resolveRenderProfileDocument(profiles,
                                                        options.overrides);
}

} // namespace LX_tools::offline_render

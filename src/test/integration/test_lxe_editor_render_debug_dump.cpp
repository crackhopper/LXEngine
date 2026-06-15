#include "editor/app/editor_session.hpp"
#include "editor/commands/command_bus.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

int fail(const std::string &message) {
  std::cerr << message << '\n';
  return 1;
}

} // namespace

int main() {
  LX_core::CommandBus bus;
  LX_demo::lxe_editor::LxeEditorSession::RenderDebugCommandHooks hooks;

  std::filesystem::path requestedPath;
  std::filesystem::path requestedScreenPath;
  int dumpCallCount = 0;
  hooks.dumpRenderTarget =
      [&requestedPath, &requestedScreenPath, &dumpCallCount](
          std::string_view targetName, const std::filesystem::path &path,
          const std::filesystem::path &screenPath)
      -> LX_demo::lxe_editor::LxeEditorSession::RenderDebugDumpResult {
    ++dumpCallCount;
    if (targetName != "hdr.color") {
      throw std::runtime_error("unexpected target");
    }
    requestedPath = path;
    requestedScreenPath = screenPath;
    return LX_demo::lxe_editor::LxeEditorSession::RenderDebugDumpResult{
        .path = path,
        .screenPath = screenPath,
        .width = 16,
        .height = 16,
        .format = "R16G16B16A16_SFLOAT",
        .maxValue = 1.0,
        .meanValue = 0.5,
        .nonZeroRatio = 1.0,
    };
  };

  LX_demo::lxe_editor::LxeEditorSession::registerRenderDebugCommand(bus, hooks);

  const LX_core::CommandResult result =
      bus.dispatch("render debug dump hdr.color");
  if (!result.ok) {
    return fail("render debug dump command failed: " + result.message);
  }
  if (requestedPath.extension() != ".png") {
    return fail("default debug dump path should use .png, got: " +
                requestedPath.generic_string());
  }
  if (requestedScreenPath.extension() != ".png") {
    return fail("paired screen dump path should use .png, got: " +
                requestedScreenPath.generic_string());
  }
  if (requestedScreenPath.parent_path() != requestedPath.parent_path() ||
      requestedScreenPath.stem().generic_string() !=
          requestedPath.stem().generic_string() + "-screen") {
    return fail("paired screen dump path should share the target path stem: " +
                requestedScreenPath.generic_string());
  }
  if (!contains(result.structured, "\"path\":\"") ||
      !contains(result.structured, ".png\"")) {
    return fail("structured dump result should report a png path: " +
                result.structured);
  }
  if (!contains(result.structured, "\"screenPath\":\"") ||
      !contains(result.structured, "-screen.png\"")) {
    return fail("structured dump result should report a paired screen path: " +
                result.structured);
  }

  const LX_core::CommandResult passDumpResult =
      bus.dispatch("render debug dump Forward");
  if (passDumpResult.ok) {
    return fail("render debug dump Forward should be rejected");
  }
  if (!contains(passDumpResult.message, "pass dump")) {
    return fail("rejected pass dump should explain the removed path: " +
                passDumpResult.message);
  }
  if (dumpCallCount != 1) {
    return fail("rejected pass dump should not call backend dump hook");
  }

  const LX_core::backend::VulkanPostProcessSettings settings;
  if (settings.bloomEnabled) {
    return fail("bloom should be disabled by default");
  }

  return 0;
}

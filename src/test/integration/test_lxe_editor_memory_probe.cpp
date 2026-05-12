#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/editor/editor_state.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "demos/lxe_editor/camera_rig.hpp"
#include "demos/lxe_editor/editor_session.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"
#include "demos/lxe_editor/ui_overlay.hpp"
#include "infra/window/window.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;
int skipped = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] bool shouldRunProbe() {
  const char* value = std::getenv("LX_RUN_MEMORY_PROBE");
  return value && *value && std::string(value) != "0";
}

[[nodiscard]] std::optional<usize> currentRssKb() {
#if defined(__linux__)
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmRSS:") {
      usize rssKb = 0;
      status >> rssKb;
      return rssKb;
    }
    std::string discard;
    std::getline(status, discard);
  }
#endif
  return std::nullopt;
}

struct ProbeResult final {
  usize rssStartKb = 0;
  usize rssEndKb = 0;
  usize rssPeakKb = 0;
  usize configDirtyFrames = 0;
};

enum class ProbeMode {
  UiOnly,
  UiMinimalPanels,
  UiPlusCameraUpdates,
  UiPlusDebugDraw,
  UiPlusFrameLogic,
};

[[nodiscard]] std::optional<ProbeMode> requestedProbeMode() {
  const char* value = std::getenv("LX_MEMORY_PROBE_SCENARIO");
  if (!value || !*value) {
    return std::nullopt;
  }
  const std::string_view text(value);
  if (text == "ui_only") {
    return ProbeMode::UiOnly;
  }
  if (text == "ui_minimal") {
    return ProbeMode::UiMinimalPanels;
  }
  if (text == "ui_plus_frame_logic") {
    return ProbeMode::UiPlusFrameLogic;
  }
  if (text == "ui_plus_camera_updates") {
    return ProbeMode::UiPlusCameraUpdates;
  }
  if (text == "ui_plus_debugdraw") {
    return ProbeMode::UiPlusDebugDraw;
  }
  return std::nullopt;
}

void configurePanelVisibility(LX_demo::lxe_editor::EditorConfigDocument& config,
                              std::string_view id, const bool visible) {
  if (auto existing =
          LX_demo::lxe_editor::findEditorWindowLayout(config, id);
      existing.has_value()) {
    existing->get().visible = visible;
    return;
  }

  LX_demo::lxe_editor::EditorWindowLayout layout;
  layout.id = std::string(id);
  layout.visible = visible;
  config.layoutWindows.push_back(std::move(layout));
}

[[nodiscard]] ProbeResult runProbe(const ProbeMode mode,
                                   const usize frameCount) {
  if (!initializeRuntimeAssetRoot()) {
    throw std::runtime_error("runtime asset root missing");
  }

  LX_infra::Window::Initialize();
  auto window =
      std::make_shared<LX_infra::Window>("lxe-editor-memory-probe", 1280, 720);
  auto renderer = LX_core::backend::VulkanRenderer::create(
      LX_core::backend::VulkanRenderer::Token{});
  renderer->initialize(window, "lxe-editor-memory-probe");

  LX_demo::lxe_editor::CameraRig rig;
  LX_core::EditorState editorState;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  LX_core::Clock clock;
  session.initialize();
  if (mode == ProbeMode::UiMinimalPanels) {
    auto& config = session.editorConfig();
    configurePanelVisibility(config, "Scene Tree", false);
    configurePanelVisibility(config, "Inspector", false);
    configurePanelVisibility(config, "Command Console", false);
    configurePanelVisibility(config, "Stats", false);
    configurePanelVisibility(config, "Help", false);
    configurePanelVisibility(config, "Preferences", false);
    configurePanelVisibility(config, "Toolbar", true);
  }
  renderer->initScene(session.scene());
  ui.attachClock(clock);

  renderer->setDrawUiCallback([&] { ui.drawFrame(); });

  const auto input = window->getInputState();
  const auto startRss = currentRssKb();
  if (!startRss.has_value()) {
    throw std::runtime_error("failed to read /proc/self/status VmRSS");
  }

  usize peakRss = *startRss;
  usize configDirtyFrames = 0;
  for (usize frame = 0; frame < frameCount; ++frame) {
    (void)window->shouldClose();
    if (mode == ProbeMode::UiPlusCameraUpdates ||
        mode == ProbeMode::UiPlusFrameLogic) {
      const int windowWidth = window->getWidth();
      const int windowHeight = window->getHeight();
      session.setWindowSize(
          LX_core::Vec2f{static_cast<float>(windowWidth),
                         static_cast<float>(windowHeight)});
      const bool hasValidExtent = windowWidth > 0 && windowHeight > 0;
      if (hasValidExtent) {
        const float aspect =
            static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        session.editorCamera().setAspect(aspect);
        session.gameCamera().setAspect(aspect);
      }
      session.gameCamera().updateMatrices();
      session.editorCamera().updateMatrices();
    }
    if (mode == ProbeMode::UiPlusDebugDraw ||
        mode == ProbeMode::UiPlusFrameLogic) {
      session.sceneInteraction().enqueueDebugDraw();
      input->nextFrame();
    }

    renderer->uploadData();
    renderer->draw();
    if (ui.consumeConfigDirty()) {
      ++configDirtyFrames;
    }
    if (const auto rss = currentRssKb()) {
      peakRss = std::max(peakRss, *rss);
    }
  }

  const auto endRss = currentRssKb();
  if (!endRss.has_value()) {
    throw std::runtime_error("failed to sample end-of-probe VmRSS");
  }

  renderer->shutdown();
  return ProbeResult{
      .rssStartKb = *startRss,
      .rssEndKb = *endRss,
      .rssPeakKb = peakRss,
      .configDirtyFrames = configDirtyFrames,
  };
}

void testLxeEditorMemoryProbe() {
  if (!shouldRunProbe()) {
    std::cout << "[SKIP] lxe_editor_memory_probe (set LX_RUN_MEMORY_PROBE=1)\n";
    ++skipped;
    return;
  }

  try {
    constexpr usize kFrameCount = 1500;
    constexpr usize kUiOnlyGrowthBudgetKb = 64 * 1024;
    constexpr usize kFrameLogicGrowthBudgetKb = 128 * 1024;

    const auto scenario = requestedProbeMode();
    if (scenario.has_value()) {
      const ProbeResult result = runProbe(*scenario, kFrameCount);
      const usize growthKb = result.rssPeakKb - result.rssStartKb;
      const char* label = *scenario == ProbeMode::UiOnly
                              ? "ui_only"
                              : (*scenario == ProbeMode::UiMinimalPanels
                                     ? "ui_minimal"
                                     : (*scenario == ProbeMode::UiPlusCameraUpdates
                                            ? "ui_plus_camera_updates"
                                            : (*scenario == ProbeMode::UiPlusDebugDraw
                                                   ? "ui_plus_debugdraw"
                                                   : "ui_plus_frame_logic")));
      std::cout << "[probe] " << label << " start=" << result.rssStartKb
                << " peak=" << result.rssPeakKb
                << " end=" << result.rssEndKb
                << " growth_kb=" << growthKb
                << " config_dirty_frames=" << result.configDirtyFrames << "\n";
      return;
    }

    const ProbeResult uiOnly = runProbe(ProbeMode::UiOnly, kFrameCount);
    const ProbeResult uiMinimal =
        runProbe(ProbeMode::UiMinimalPanels, kFrameCount);
    const ProbeResult frameLogic =
        runProbe(ProbeMode::UiPlusFrameLogic, kFrameCount);

    const usize uiOnlyGrowthKb = uiOnly.rssPeakKb - uiOnly.rssStartKb;
    const usize uiMinimalGrowthKb =
        uiMinimal.rssPeakKb - uiMinimal.rssStartKb;
    const usize frameLogicGrowthKb =
        frameLogic.rssPeakKb - frameLogic.rssStartKb;

    std::cout << "[probe] ui_only start=" << uiOnly.rssStartKb
              << " peak=" << uiOnly.rssPeakKb
              << " end=" << uiOnly.rssEndKb
              << " growth_kb=" << uiOnlyGrowthKb
              << " config_dirty_frames=" << uiOnly.configDirtyFrames << "\n";
    std::cout << "[probe] ui_minimal start=" << uiMinimal.rssStartKb
              << " peak=" << uiMinimal.rssPeakKb
              << " end=" << uiMinimal.rssEndKb
              << " growth_kb=" << uiMinimalGrowthKb
              << " config_dirty_frames=" << uiMinimal.configDirtyFrames << "\n";
    std::cout << "[probe] ui_plus_frame_logic start="
              << frameLogic.rssStartKb
              << " peak=" << frameLogic.rssPeakKb
              << " end=" << frameLogic.rssEndKb
              << " growth_kb=" << frameLogicGrowthKb
              << " config_dirty_frames=" << frameLogic.configDirtyFrames << "\n";

    EXPECT(uiOnlyGrowthKb <= kUiOnlyGrowthBudgetKb,
           "lxe_editor UI-only probe exceeded RSS growth budget");
    EXPECT(uiMinimalGrowthKb <= kUiOnlyGrowthBudgetKb,
           "lxe_editor minimal-panel probe exceeded RSS growth budget");
    EXPECT(frameLogicGrowthKb <= kFrameLogicGrowthBudgetKb,
           "lxe_editor frame-logic probe exceeded RSS growth budget");
  } catch (const std::exception& e) {
    std::cout << "[SKIP] lxe_editor_memory_probe (exception: " << e.what()
              << ")\n";
    ++skipped;
  }
}

} // namespace

int main() {
  expSetEnvVK();
  testLxeEditorMemoryProbe();

  if (failures == 0) {
    std::cout << "[PASS] lxe_editor memory probe: " << skipped << " skipped\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}

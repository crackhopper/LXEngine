#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/debug_draw/debug_draw.hpp"
#include "core/editor/editor_state.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"
#include "demos/lxe_editor/camera_rig.hpp"
#include "demos/lxe_editor/editor_session.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"
#include "demos/lxe_editor/ui_overlay.hpp"

#include <chrono>
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
  const char *value = std::getenv("LX_RUN_MEMORY_PROBE");
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
  usize rssBeforeLoadKb = 0;
  usize rssAfterLoadKb = 0;
  usize rssStartKb = 0;
  usize rssEndKb = 0;
  usize rssPeakKb = 0;
  usize configDirtyFrames = 0;
  usize rendererCacheStart = 0;
  usize rendererCacheEnd = 0;
  usize rendererCachePeak = 0;
  usize frameGraphItemCount = 0;
  usize initSceneCallCount = 0;
  usize debugVertexCapacityPeak = 0;
  usize debugIndexCapacityPeak = 0;
  usize debugVertexIdentityChanges = 0;
  usize debugIndexIdentityChanges = 0;
};

enum class ProbeMode {
  UiOnly,
  UiMinimalPanels,
  UiPlusCameraUpdates,
  UiPlusDebugDraw,
  UiPlusFrameLogic,
  ProjectUiOnly,
  ProjectUiPlusFrameLogic,
};

[[nodiscard]] std::optional<ProbeMode> requestedProbeMode() {
  const char *value = std::getenv("LX_MEMORY_PROBE_SCENARIO");
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
  if (text == "project_ui_only") {
    return ProbeMode::ProjectUiOnly;
  }
  if (text == "project_ui_plus_frame_logic") {
    return ProbeMode::ProjectUiPlusFrameLogic;
  }
  return std::nullopt;
}

[[nodiscard]] usize requestedFrameCount(const usize fallback) {
  const char *value = std::getenv("LX_MEMORY_PROBE_FRAMES");
  if (!value || !*value) {
    return fallback;
  }
  try {
    const auto parsed = static_cast<usize>(std::stoul(value));
    return parsed == 0 ? fallback : parsed;
  } catch (...) {
    return fallback;
  }
}

[[nodiscard]] bool usesProjectScene(const ProbeMode mode) {
  return mode == ProbeMode::ProjectUiOnly ||
         mode == ProbeMode::ProjectUiPlusFrameLogic;
}

[[nodiscard]] bool runsFrameLogic(const ProbeMode mode) {
  return mode == ProbeMode::UiPlusFrameLogic ||
         mode == ProbeMode::ProjectUiPlusFrameLogic;
}

[[nodiscard]] bool runsCameraUpdates(const ProbeMode mode) {
  return mode == ProbeMode::UiPlusCameraUpdates || runsFrameLogic(mode);
}

[[nodiscard]] bool runsDebugDraw(const ProbeMode mode) {
  return mode == ProbeMode::UiPlusDebugDraw || runsFrameLogic(mode);
}

[[nodiscard]] const char *labelForProbeMode(const ProbeMode mode) {
  switch (mode) {
  case ProbeMode::UiOnly:
    return "ui_only";
  case ProbeMode::UiMinimalPanels:
    return "ui_minimal";
  case ProbeMode::UiPlusCameraUpdates:
    return "ui_plus_camera_updates";
  case ProbeMode::UiPlusDebugDraw:
    return "ui_plus_debugdraw";
  case ProbeMode::UiPlusFrameLogic:
    return "ui_plus_frame_logic";
  case ProbeMode::ProjectUiOnly:
    return "project_ui_only";
  case ProbeMode::ProjectUiPlusFrameLogic:
    return "project_ui_plus_frame_logic";
  }
  return "unknown";
}

void loadProbeProjectScene(LX_demo::lxe_editor::LxeEditorSession &session,
                           LX_core::gpu::EngineLoop &loop) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto result = session.commandBus().dispatch(
      "project init empty memory_probe_project_" + std::to_string(suffix));
  if (!result.ok) {
    throw std::runtime_error("failed to queue probe project scene: " +
                             result.message);
  }
  session.flushPendingSceneOpen(loop);
  if (!session.runtimeScenePath().has_value() ||
      session.currentProjectActiveScene() !=
          std::optional<std::string>("scenes/main.scene.yaml") ||
      session.scene()->getSceneName() != "Empty Project") {
    throw std::runtime_error("probe project scene did not bind at runtime");
  }
}

void configurePanelVisibility(LX_demo::lxe_editor::EditorConfigDocument &config,
                              std::string_view id, const bool visible) {
  if (auto existing = LX_demo::lxe_editor::findEditorWindowLayout(config, id);
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
  auto renderer = std::shared_ptr<LX_core::backend::VulkanRenderer>(
      LX_core::backend::VulkanRenderer::create(
          LX_core::backend::VulkanRenderer::Token{})
          .release());
  renderer->initialize(window, "lxe-editor-memory-probe");

  LX_demo::lxe_editor::CameraRig rig;
  LX_core::EditorState editorState;
  LX_demo::lxe_editor::UiOverlay ui;
  LX_demo::lxe_editor::LxeEditorSession session(rig, ui, editorState);
  session.initialize();
  if (mode == ProbeMode::UiMinimalPanels) {
    auto &config = session.editorConfig();
    configurePanelVisibility(config, "Scene Tree", false);
    configurePanelVisibility(config, "Inspector", false);
    configurePanelVisibility(config, "Command Console", false);
    configurePanelVisibility(config, "Stats", false);
    configurePanelVisibility(config, "Help", false);
    configurePanelVisibility(config, "Preferences", false);
    configurePanelVisibility(config, "Toolbar", true);
  }
  LX_core::gpu::EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(session.scene());
  ui.attachClock(loop.getClock());

  usize configDirtyFrames = 0;
  renderer->setDrawUiCallback([&] {
    ui.drawFrame(LX_core::Vec2f{static_cast<float>(window->getWidth()),
                                static_cast<float>(window->getHeight())});
    if (ui.consumeConfigDirty()) {
      ++configDirtyFrames;
    }
  });

  if (usesProjectScene(mode)) {
    loadProbeProjectScene(session, loop);
  }

  const auto input = window->getInputState();
  const auto rssAfterLoad = currentRssKb();
  if (!rssAfterLoad.has_value()) {
    throw std::runtime_error("failed to read /proc/self/status VmRSS");
  }

  loop.setUpdateHook([&](LX_core::Scene &, const LX_core::Clock &clock) {
    const int windowWidth = window->getWidth();
    const int windowHeight = window->getHeight();
    session.setWindowSize(LX_core::Vec2f{static_cast<float>(windowWidth),
                                         static_cast<float>(windowHeight)});
    const bool hasValidExtent = windowWidth > 0 && windowHeight > 0;
    if (runsCameraUpdates(mode) && hasValidExtent) {
      const float aspect =
          static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
      session.editorCamera().setAspect(aspect);
      session.gameCamera().setAspect(aspect);
    }
    if (runsCameraUpdates(mode)) {
      session.gameCamera().updateMatrices();
      session.editorCamera().updateMatrices();
    }
    if (runsDebugDraw(mode)) {
      session.sceneInteraction().enqueueDebugDraw();
      input->nextFrame();
    }
    (void)clock;
  });

  if (window->shouldClose()) {
    throw std::runtime_error("window closed before probe started");
  }
  loop.tickFrame();

  const auto startRss = currentRssKb();
  if (!startRss.has_value()) {
    throw std::runtime_error("failed to sample post-warmup VmRSS");
  }
  const usize rendererCacheStart = renderer->cachedResourceCount();

  const auto debugVertexIdentityFor = [] {
    return LX_core::DebugDraw::testing::vertexBufferIdentity(
        LX_core::Layer_EditorOverlay);
  };
  const auto debugIndexIdentityFor = [] {
    return LX_core::DebugDraw::testing::indexBufferIdentity(
        LX_core::Layer_EditorOverlay);
  };

  usize peakRss = *startRss;
  usize peakRendererCache = renderer->cachedResourceCount();
  usize peakDebugVertexCapacity =
      LX_core::DebugDraw::testing::reservedVertexCapacity(
          LX_core::Layer_EditorOverlay);
  usize peakDebugIndexCapacity =
      LX_core::DebugDraw::testing::reservedIndexCapacity(
          LX_core::Layer_EditorOverlay);
  LX_core::ResourceCacheIdentity previousDebugVertexIdentity =
      debugVertexIdentityFor();
  LX_core::ResourceCacheIdentity previousDebugIndexIdentity =
      debugIndexIdentityFor();
  usize debugVertexIdentityChanges = 0;
  usize debugIndexIdentityChanges = 0;
  for (usize frame = 0; frame < frameCount; ++frame) {
    (void)window->shouldClose();
    loop.tickFrame();
    if (const auto rss = currentRssKb()) {
      peakRss = std::max(peakRss, *rss);
    }
    peakRendererCache =
        std::max(peakRendererCache, renderer->cachedResourceCount());
    peakDebugVertexCapacity =
        std::max(peakDebugVertexCapacity,
                 LX_core::DebugDraw::testing::reservedVertexCapacity(
                     LX_core::Layer_EditorOverlay));
    peakDebugIndexCapacity =
        std::max(peakDebugIndexCapacity,
                 LX_core::DebugDraw::testing::reservedIndexCapacity(
                     LX_core::Layer_EditorOverlay));

    const LX_core::ResourceCacheIdentity currentDebugVertexIdentity =
        debugVertexIdentityFor();
    const LX_core::ResourceCacheIdentity currentDebugIndexIdentity =
        debugIndexIdentityFor();
    if (currentDebugVertexIdentity != 0 && previousDebugVertexIdentity != 0 &&
        currentDebugVertexIdentity != previousDebugVertexIdentity) {
      ++debugVertexIdentityChanges;
    }
    if (currentDebugIndexIdentity != 0 && previousDebugIndexIdentity != 0 &&
        currentDebugIndexIdentity != previousDebugIndexIdentity) {
      ++debugIndexIdentityChanges;
    }
    previousDebugVertexIdentity = currentDebugVertexIdentity;
    previousDebugIndexIdentity = currentDebugIndexIdentity;
  }

  const auto endRss = currentRssKb();
  if (!endRss.has_value()) {
    throw std::runtime_error("failed to sample end-of-probe VmRSS");
  }

  const usize rendererCacheEnd = renderer->cachedResourceCount();
  const usize frameGraphItemCount = renderer->frameGraphItemCount();
  const usize initSceneCallCount = renderer->initSceneCallCount();
  renderer->shutdown();
  return ProbeResult{
      .rssBeforeLoadKb = *rssAfterLoad,
      .rssAfterLoadKb = *startRss,
      .rssStartKb = *startRss,
      .rssEndKb = *endRss,
      .rssPeakKb = peakRss,
      .configDirtyFrames = configDirtyFrames,
      .rendererCacheStart = rendererCacheStart,
      .rendererCacheEnd = rendererCacheEnd,
      .rendererCachePeak = peakRendererCache,
      .frameGraphItemCount = frameGraphItemCount,
      .initSceneCallCount = initSceneCallCount,
      .debugVertexCapacityPeak = peakDebugVertexCapacity,
      .debugIndexCapacityPeak = peakDebugIndexCapacity,
      .debugVertexIdentityChanges = debugVertexIdentityChanges,
      .debugIndexIdentityChanges = debugIndexIdentityChanges,
  };
}

void testLxeEditorMemoryProbe() {
  if (!shouldRunProbe()) {
    std::cout << "[SKIP] lxe_editor_memory_probe (set LX_RUN_MEMORY_PROBE=1)\n";
    ++skipped;
    return;
  }

  try {
    constexpr usize kDefaultFrameCount = 1500;
    constexpr usize kUiOnlyGrowthBudgetKb = 64 * 1024;
    constexpr usize kFrameLogicGrowthBudgetKb = 128 * 1024;
    const usize frameCount = requestedFrameCount(kDefaultFrameCount);

    const auto scenario = requestedProbeMode();
    if (scenario.has_value()) {
      const ProbeResult result = runProbe(*scenario, frameCount);
      const usize growthKb = result.rssPeakKb - result.rssStartKb;
      const char *label = labelForProbeMode(*scenario);
      std::cout << "[probe] " << label << " frames=" << frameCount
                << " after_load=" << result.rssBeforeLoadKb
                << " post_warmup=" << result.rssStartKb
                << " peak=" << result.rssPeakKb << " end=" << result.rssEndKb
                << " growth_kb=" << growthKb
                << " config_dirty_frames=" << result.configDirtyFrames
                << " renderer_cache_peak=" << result.rendererCachePeak
                << " frame_graph_items=" << result.frameGraphItemCount
                << " init_scene_calls=" << result.initSceneCallCount
                << " debug_vertex_cap_peak=" << result.debugVertexCapacityPeak
                << " debug_index_cap_peak=" << result.debugIndexCapacityPeak
                << " debug_vertex_identity_changes="
                << result.debugVertexIdentityChanges
                << " debug_index_identity_changes="
                << result.debugIndexIdentityChanges << "\n";
      return;
    }

    const ProbeResult uiOnly = runProbe(ProbeMode::UiOnly, frameCount);
    const ProbeResult uiMinimal =
        runProbe(ProbeMode::UiMinimalPanels, frameCount);
    const ProbeResult frameLogic =
        runProbe(ProbeMode::UiPlusFrameLogic, frameCount);
    const ProbeResult projectFrameLogic =
        runProbe(ProbeMode::ProjectUiPlusFrameLogic, frameCount);

    const usize uiOnlyGrowthKb = uiOnly.rssPeakKb - uiOnly.rssStartKb;
    const usize uiMinimalGrowthKb = uiMinimal.rssPeakKb - uiMinimal.rssStartKb;
    const usize frameLogicGrowthKb =
        frameLogic.rssPeakKb - frameLogic.rssStartKb;
    const usize projectFrameLogicGrowthKb =
        projectFrameLogic.rssPeakKb - projectFrameLogic.rssStartKb;

    std::cout << "[probe] ui_only start=" << uiOnly.rssStartKb
              << " peak=" << uiOnly.rssPeakKb << " end=" << uiOnly.rssEndKb
              << " growth_kb=" << uiOnlyGrowthKb
              << " config_dirty_frames=" << uiOnly.configDirtyFrames << "\n";
    std::cout << "[probe] ui_minimal start=" << uiMinimal.rssStartKb
              << " peak=" << uiMinimal.rssPeakKb
              << " end=" << uiMinimal.rssEndKb
              << " growth_kb=" << uiMinimalGrowthKb
              << " config_dirty_frames=" << uiMinimal.configDirtyFrames << "\n";
    std::cout << "[probe] ui_plus_frame_logic start=" << frameLogic.rssStartKb
              << " peak=" << frameLogic.rssPeakKb
              << " end=" << frameLogic.rssEndKb
              << " growth_kb=" << frameLogicGrowthKb
              << " config_dirty_frames=" << frameLogic.configDirtyFrames
              << "\n";
    std::cout << "[probe] project_ui_plus_frame_logic after_load="
              << projectFrameLogic.rssBeforeLoadKb
              << " post_warmup=" << projectFrameLogic.rssStartKb
              << " peak=" << projectFrameLogic.rssPeakKb
              << " end=" << projectFrameLogic.rssEndKb
              << " growth_kb=" << projectFrameLogicGrowthKb
              << " renderer_cache_peak=" << projectFrameLogic.rendererCachePeak
              << " frame_graph_items=" << projectFrameLogic.frameGraphItemCount
              << " init_scene_calls=" << projectFrameLogic.initSceneCallCount
              << " debug_vertex_cap_peak="
              << projectFrameLogic.debugVertexCapacityPeak
              << " debug_index_cap_peak="
              << projectFrameLogic.debugIndexCapacityPeak
              << " debug_vertex_identity_changes="
              << projectFrameLogic.debugVertexIdentityChanges
              << " debug_index_identity_changes="
              << projectFrameLogic.debugIndexIdentityChanges << "\n";

    EXPECT(uiOnlyGrowthKb <= kUiOnlyGrowthBudgetKb,
           "lxe_editor UI-only probe exceeded RSS growth budget");
    EXPECT(uiMinimalGrowthKb <= kUiOnlyGrowthBudgetKb,
           "lxe_editor minimal-panel probe exceeded RSS growth budget");
    EXPECT(frameLogicGrowthKb <= kFrameLogicGrowthBudgetKb,
           "lxe_editor frame-logic probe exceeded RSS growth budget");
  } catch (const std::exception &e) {
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

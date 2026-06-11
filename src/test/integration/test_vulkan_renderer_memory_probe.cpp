#include "core/asset/mesh.hpp"
#include "core/asset/skeleton.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/renderer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"

#if defined(USE_SDL)
#include "backend/vulkan/vulkan_renderer.hpp"
#include "infra/window/window.hpp"
#include <imgui.h>
#endif

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

using namespace LX_core;

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

[[nodiscard]] usize requestedFrameCount(const usize fallback) {
  const char* value = std::getenv("LX_MEMORY_PROBE_FRAMES");
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

[[nodiscard]] SceneSharedPtr makeStaticScene() {
  using V = VertexPosNormalUvBone;
  auto vertexBuffer = VertexBuffer<V>::create({
      V({-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      V({1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      V({1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 1.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
  });
  auto indexBuffer = IndexBuffer::create({0u, 1u, 2u});
  auto mesh = Mesh::create(vertexBuffer, indexBuffer,
                           BoundingBox{{-1.0f, -1.0f, 0.0f},
                                       {1.0f, 1.0f, 0.0f}});

  auto material =
      LX_infra::loadGenericMaterial("assets/materials/pbr.material");
  material->setParameter(StringID("MaterialUBO"), StringID("enableNormal"), 0);
  material->syncGpuData();

  auto node = SceneNode::create("memory_probe_triangle");
  node->addComponent<MeshComponent>(mesh);
  node->addComponent<MaterialComponent>(material);
  node->addComponent<SkeletonComponent>(Skeleton::create({}));

  auto scene = Scene::create(node);

  auto cameraNode = SceneNode::create("memory_probe_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  if (!camera.has_value()) {
    throw std::runtime_error("memory probe camera component missing");
  }
  camera->get().lookAt({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f},
                       {0.0f, 1.0f, 0.0f});
  camera->get().updateMatrices();
  scene->addCamera(cameraNode);
  return scene;
}

#if defined(USE_SDL)
struct ProbeResult final {
  usize rssStartKb = 0;
  usize rssEndKb = 0;
  usize rssPeakKb = 0;
};

enum class ProbeUiMode {
  None,
  SimpleText,
  MultiWindowWidgets,
};

struct RendererProbeScenario final {
  ProbeUiMode uiMode = ProbeUiMode::None;
  bool initializeScene = false;
  const char* label = "no_scene_no_ui";
};

[[nodiscard]] std::optional<RendererProbeScenario> requestedScenario() {
  const char* value = std::getenv("LX_RENDERER_MEMORY_PROBE_SCENARIO");
  if (!value || !*value) {
    return std::nullopt;
  }
  const std::string_view text(value);
  if (text == "no_scene_no_ui") {
    return RendererProbeScenario{
        .uiMode = ProbeUiMode::None,
        .initializeScene = false,
        .label = "no_scene_no_ui"};
  }
  if (text == "no_ui") {
    return RendererProbeScenario{
        .uiMode = ProbeUiMode::None,
        .initializeScene = true,
        .label = "no_ui"};
  }
  if (text == "simple_ui") {
    return RendererProbeScenario{
        .uiMode = ProbeUiMode::SimpleText,
        .initializeScene = true,
        .label = "simple_ui"};
  }
  if (text == "multi_window_ui") {
    return RendererProbeScenario{
        .uiMode = ProbeUiMode::MultiWindowWidgets,
        .initializeScene = true,
        .label = "multi_window_ui"};
  }
  return std::nullopt;
}

[[nodiscard]] ProbeResult runRendererProbe(const ProbeUiMode uiMode,
                                           const usize frameCount,
                                           const bool initializeScene) {
  if (!initializeRuntimeAssetRoot()) {
    throw std::runtime_error("runtime asset root missing");
  }

  auto window =
      std::make_shared<LX_infra::Window>("renderer-memory-probe", 800, 600);
  auto renderer = backend::VulkanRenderer::create(backend::VulkanRenderer::Token{});
  renderer->initialize(window, "renderer-memory-probe");
  if (initializeScene) {
    renderer->initScene(makeStaticScene());
  }
  if (uiMode != ProbeUiMode::None) {
    renderer->setDrawUiCallback([uiMode] {
      if (uiMode == ProbeUiMode::SimpleText) {
        ImGui::TextUnformatted("memory probe");
        return;
      }

      for (int i = 0; i < 5; ++i) {
        std::string title = "Probe Window " + std::to_string(i);
        ImGui::SetNextWindowPos(ImVec2(20.0f + i * 40.0f, 20.0f + i * 24.0f),
                                ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(260.0f, 120.0f), ImGuiCond_Appearing);
        if (ImGui::Begin(title.c_str())) {
          ImGui::Text("window %d", i);
          bool enabled = (i % 2) == 0;
          ImGui::Checkbox("enabled", &enabled);
          float slider = static_cast<float>(i) * 0.1f;
          ImGui::SliderFloat("value", &slider, 0.0f, 1.0f);
          ImGui::Button("action");
        }
        ImGui::End();
      }
    });
  }

  const auto startRss = currentRssKb();
  if (!startRss.has_value()) {
    throw std::runtime_error("failed to read /proc/self/status VmRSS");
  }

  usize peakRss = *startRss;
  for (usize frame = 0; frame < frameCount; ++frame) {
    renderer->uploadData();
    renderer->draw();
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
  };
}
#endif

void testRendererMemoryProbe() {
  if (!shouldRunProbe()) {
    std::cout << "[SKIP] renderer_memory_probe (set LX_RUN_MEMORY_PROBE=1)\n";
    ++skipped;
    return;
  }

#if !defined(USE_SDL)
  std::cout << "[SKIP] renderer_memory_probe (USE_SDL not defined)\n";
  ++skipped;
  return;
#else
  try {
    LX_infra::Window::Initialize();
    constexpr usize kDefaultFrameCount = 1500;
    constexpr usize kMaxNoUiGrowthKb = 64 * 1024;
    constexpr usize kMaxUiGrowthKb = 96 * 1024;
    const usize frameCount = requestedFrameCount(kDefaultFrameCount);
    const auto scenario = requestedScenario();

    if (scenario.has_value()) {
      const ProbeResult result = runRendererProbe(
          scenario->uiMode, frameCount, scenario->initializeScene);
      const usize growthKb = result.rssPeakKb - result.rssStartKb;
      std::cout << "[probe] " << scenario->label
                << " frames=" << frameCount
                << " start=" << result.rssStartKb
                << " peak=" << result.rssPeakKb
                << " end=" << result.rssEndKb
                << " growth_kb=" << growthKb << "\n";
      return;
    }

    const ProbeResult noSceneNoUi =
        runRendererProbe(ProbeUiMode::None, frameCount, false);
    const ProbeResult noUi =
        runRendererProbe(ProbeUiMode::None, frameCount, true);
    const ProbeResult simpleUi =
        runRendererProbe(ProbeUiMode::SimpleText, frameCount, true);
    const ProbeResult multiWindowUi =
        runRendererProbe(ProbeUiMode::MultiWindowWidgets, frameCount, true);

    const usize noSceneNoUiGrowthKb =
        noSceneNoUi.rssPeakKb - noSceneNoUi.rssStartKb;
    const usize noUiGrowthKb = noUi.rssPeakKb - noUi.rssStartKb;
    const usize simpleUiGrowthKb = simpleUi.rssPeakKb - simpleUi.rssStartKb;
    const usize multiWindowUiGrowthKb =
        multiWindowUi.rssPeakKb - multiWindowUi.rssStartKb;

    std::cout << "[probe] no_scene_no_ui frames=" << frameCount
              << " start=" << noSceneNoUi.rssStartKb
              << " peak=" << noSceneNoUi.rssPeakKb
              << " end=" << noSceneNoUi.rssEndKb
              << " growth_kb=" << noSceneNoUiGrowthKb << "\n";
    std::cout << "[probe] no_ui frames=" << frameCount
              << " start=" << noUi.rssStartKb
              << " peak=" << noUi.rssPeakKb
              << " end=" << noUi.rssEndKb
              << " growth_kb=" << noUiGrowthKb << "\n";
    std::cout << "[probe] simple_ui frames=" << frameCount
              << " start=" << simpleUi.rssStartKb
              << " peak=" << simpleUi.rssPeakKb
              << " end=" << simpleUi.rssEndKb
              << " growth_kb=" << simpleUiGrowthKb << "\n";
    std::cout << "[probe] multi_window_ui frames=" << frameCount
              << " start=" << multiWindowUi.rssStartKb
              << " peak=" << multiWindowUi.rssPeakKb
              << " end=" << multiWindowUi.rssEndKb
              << " growth_kb=" << multiWindowUiGrowthKb << "\n";

    EXPECT(noSceneNoUiGrowthKb <= kMaxNoUiGrowthKb,
           "swapchain-only probe exceeded no-scene RSS growth budget");
    EXPECT(noUiGrowthKb <= kMaxNoUiGrowthKb,
           "pure VulkanRenderer probe exceeded no-UI RSS growth budget");
    EXPECT(simpleUiGrowthKb <= kMaxUiGrowthKb,
           "VulkanRenderer + simple ImGui probe exceeded UI RSS growth budget");
    EXPECT(multiWindowUiGrowthKb <= kMaxUiGrowthKb,
           "VulkanRenderer + multi-window ImGui probe exceeded UI RSS growth budget");
  } catch (const std::exception &e) {
    std::cout << "[SKIP] renderer_memory_probe (exception: " << e.what()
              << ")\n";
    ++skipped;
  }
#endif
}

} // namespace

int main() {
  expSetEnvVK();
  testRendererMemoryProbe();

  if (failures == 0) {
    std::cout << "[PASS] vulkan renderer memory probe: " << skipped
              << " skipped\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}

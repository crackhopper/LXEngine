#include "core/debug_draw/debug_draw.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace LX_core;

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

SceneSharedPtr makeSceneWithCamera(VisibilityLayerMask mask) {
  auto scene = Scene::create("debug_draw_test");
  auto cameraNode = SceneNode::create("debug_camera");
  auto camera = cameraNode->addComponent<CameraComponent>();
  EXPECT(camera.has_value(), "camera component should attach");
  camera->get().setTarget(RenderTarget{});
  camera->get().setCullingMask(mask);
  camera->get().updateMatrices();
  scene->addCamera(cameraNode);
  return scene;
}

usize buildOverlayQueueItemCount(Scene &scene) {
  RenderQueue queue;
  queue.buildFromScene(scene, Pass_DebugOverlay, RenderTarget{});
  return queue.getItems().size();
}

void resetForScene(const SceneSharedPtr &scene) {
  DebugDraw::reset();
  DebugDraw::attachScene(scene);
}

void testDrawLineFlushesSixVertices() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::drawLine({0, 0, 0}, {0, 1, 0});
  DebugDraw::drawLine({0, 0, 0}, {0, 0, 1});
  const bool sceneChanged = DebugDraw::endFrame();

  EXPECT(!sceneChanged,
         "prewarmed overlay bucket should avoid first-frame scene rebuild");
  EXPECT(DebugDraw::testing::queuedLineCount() == 3, "three lines queued");
  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 6,
         "three lines flush six vertices");
}

void testAttachScenePrewarmsOverlayBucket() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  EXPECT(DebugDraw::testing::hasRenderable(Layer_EditorOverlay),
         "attachScene should prewarm the default overlay bucket");
  EXPECT(buildOverlayQueueItemCount(*scene) == 1,
         "prewarmed overlay bucket should participate in debug-overlay queue build");
}

void testWireSphereSegmentsMatchThreeGreatCircles() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::wireSphere({0, 0, 0}, 1.0f, DebugDraw::Color::white(), 24);
  DebugDraw::endFrame();

  EXPECT(DebugDraw::testing::queuedLineCount() == 72,
         "wireSphere(24) should emit 72 lines");
  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 144,
         "wireSphere(24) should flush 144 vertices");
}

void testFrustumProducesTwelveLines() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::frustum(Mat4f::identity());
  DebugDraw::endFrame();

  EXPECT(DebugDraw::testing::queuedLineCount() == 12,
         "frustum should emit 12 lines");
  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 24,
         "frustum should flush 24 vertices");
}

void testOverlayHiddenFromGameCameraMask() {
  auto scene = makeSceneWithCamera(Layer_Default);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::endFrame();

  EXPECT(buildOverlayQueueItemCount(*scene) == 0,
         "game camera mask should hide editor overlay renderable");
}

void testOverlayVisibleToEditorCameraMask() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::endFrame();

  EXPECT(buildOverlayQueueItemCount(*scene) == 1,
         "editor camera mask should include overlay renderable");
}

void testBeginFrameClearsPreviousGeometry() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::endFrame();

  DebugDraw::beginFrame();
  DebugDraw::endFrame();

  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 0,
         "empty second frame should clear previous vertices");
}

void testLineLimitClipsNewestLines() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  for (usize i = 0; i < 100010; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                        {static_cast<float>(i), 0.0f, 0.0f});
  }
  DebugDraw::endFrame();

  EXPECT(DebugDraw::testing::queuedLineCount() == 100000,
         "line limit should keep only first 100000 lines");
  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 200000,
         "line limit should flush exactly 200000 vertices");
}

void testConcurrentDrawLineAccumulatesSafely() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  constexpr int kThreadCount = 4;
  constexpr int kLinesPerThread = 25;

  DebugDraw::beginFrame();
  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);
  for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
    workers.emplace_back([threadIndex]() {
      for (int lineIndex = 0; lineIndex < kLinesPerThread; ++lineIndex) {
        const float x = static_cast<float>(threadIndex);
        const float y = static_cast<float>(lineIndex);
        DebugDraw::drawLine({x, y, 0.0f}, {x, y, 1.0f});
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
  const bool sceneChanged = DebugDraw::endFrame();

  EXPECT(!sceneChanged, "concurrent writes should reuse the prewarmed bucket");
  EXPECT(DebugDraw::testing::queuedLineCount() ==
             static_cast<usize>(kThreadCount * kLinesPerThread),
         "concurrent line submissions should all be retained");
  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) ==
             static_cast<usize>(kThreadCount * kLinesPerThread * 2),
         "concurrent line submissions should flush expected vertex count");
}

} // namespace

int main() {
  expSetEnvVK();
  initializeRuntimeAssetRoot();

  testAttachScenePrewarmsOverlayBucket();
  testDrawLineFlushesSixVertices();
  testWireSphereSegmentsMatchThreeGreatCircles();
  testFrustumProducesTwelveLines();
  testOverlayHiddenFromGameCameraMask();
  testOverlayVisibleToEditorCameraMask();
  testBeginFrameClearsPreviousGeometry();
  testLineLimitClipsNewestLines();
  testConcurrentDrawLineAccumulatesSafely();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }

  std::cout << "OK: all debug_draw tests passed\n";
  return 0;
}

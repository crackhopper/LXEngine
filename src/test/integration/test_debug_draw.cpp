#include "core/debug_draw/debug_draw.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <iostream>
#include <memory>
#include <string>

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
  RenderWorkQueue queue;
  queue.build(LX_core::RenderWorkBuildContext::realtime(scene), Pass_DebugOverlay, RenderTarget{});
  return queue.getItems().size();
}

std::vector<RenderWorkItem> buildOverlayQueueItems(Scene &scene) {
  RenderWorkQueue queue;
  queue.build(LX_core::RenderWorkBuildContext::realtime(scene), Pass_DebugOverlay, RenderTarget{});
  return queue.getItems();
}

void resetForScene(const SceneSharedPtr &scene) {
  DebugDraw::reset();
  DebugDraw::setMaterialProvider({});
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

  EXPECT(sceneChanged, "first debug bucket should request scene rebuild");
  EXPECT(DebugDraw::testing::queuedLineCount() == 3, "three lines queued");
  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 6,
         "three lines flush six vertices");
}

void testDebugDrawBucketDoesNotEnterEditableSceneTree() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::endFrame();

  EXPECT(DebugDraw::testing::hasRenderable(Layer_EditorOverlay),
         "debug draw should still create a renderable bucket");
  bool foundDebugOnlyBucket = false;
  for (const auto &renderable : scene->getRenderables()) {
    if (renderable && renderable->getNodeName().rfind("debug_draw_", 0) == 0) {
      foundDebugOnlyBucket = renderable->isDebugOnlyRenderable();
    }
  }
  EXPECT(foundDebugOnlyBucket,
         "debug draw bucket should be marked debug-only at renderable level");
  EXPECT(scene->findByPath("/debug_draw_2147483648") == nullptr,
         "debug draw bucket should not be addressable as an editable scene node");
  EXPECT(scene->dumpTree().find("debug_draw_") == std::string::npos,
         "debug draw bucket should not appear in the scene tree");
}

void testDebugDrawUsesInjectedMaterialAsset() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  auto material =
      LX_infra::loadGenericMaterial("assets/materials/debug_line.material");
  int providerCalls = 0;
  DebugDraw::setMaterialProvider([&, material] {
    ++providerCalls;
    return material;
  });

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  DebugDraw::endFrame();

  const auto items = buildOverlayQueueItems(*scene);
  EXPECT(providerCalls == 1,
         "debug draw should request the injected material once");
  EXPECT(items.size() == 1, "debug draw should produce one overlay item");
  if (!items.empty()) {
    EXPECT(items[0].material == material,
           "debug draw render item should use injected material asset");
  }
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

void testBucketCapacityGrowsForLargerLaterFrame() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  const bool firstFrameChanged = DebugDraw::endFrame();
  const usize initialCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);
  const auto initialVertexIdentity =
      DebugDraw::testing::vertexBufferIdentity(Layer_EditorOverlay);
  const auto initialIndexIdentity =
      DebugDraw::testing::indexBufferIdentity(Layer_EditorOverlay);

  DebugDraw::beginFrame();
  for (int i = 0; i < 129; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                        {static_cast<float>(i), 1.0f, 0.0f});
  }
  const bool growthFrameChanged = DebugDraw::endFrame();

  const usize grownCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);
  const usize grownIndexCapacity =
      DebugDraw::testing::reservedIndexCapacity(Layer_EditorOverlay);
  const usize bufferedVertexCapacity =
      DebugDraw::testing::bufferedVertexCapacity(Layer_EditorOverlay);
  const usize bufferedIndexCapacity =
      DebugDraw::testing::bufferedIndexCapacity(Layer_EditorOverlay);
  const auto grownVertexIdentity =
      DebugDraw::testing::vertexBufferIdentity(Layer_EditorOverlay);
  const auto grownIndexIdentity =
      DebugDraw::testing::indexBufferIdentity(Layer_EditorOverlay);
  EXPECT(firstFrameChanged, "first debug bucket should request scene rebuild");
  EXPECT(growthFrameChanged,
         "capacity growth on an existing bucket should request scene rebuild");
  EXPECT(grownCapacity > initialCapacity,
         "later larger frame should grow reserved capacity");
  EXPECT(grownCapacity >= 258,
         "capacity should grow to cover the larger frame");
  EXPECT(grownIndexCapacity == grownCapacity,
         "line bucket should retain matching vertex/index capacity");
  EXPECT(bufferedVertexCapacity == grownCapacity,
         "vertex buffer payload should match retained capacity");
  EXPECT(bufferedIndexCapacity == grownIndexCapacity,
         "index buffer payload should match retained capacity");
  EXPECT(grownVertexIdentity != initialVertexIdentity,
         "growth should replace vertex buffer identity");
  EXPECT(grownIndexIdentity != initialIndexIdentity,
         "growth should replace index buffer identity");
}

void testLaterWithinCapacityFrameDoesNotRequestAnotherRebuild() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  for (int i = 0; i < 129; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                        {static_cast<float>(i), 1.0f, 0.0f});
  }
  DebugDraw::endFrame();
  const usize retainedCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);
  const auto retainedVertexIdentity =
      DebugDraw::testing::vertexBufferIdentity(Layer_EditorOverlay);
  const auto retainedIndexIdentity =
      DebugDraw::testing::indexBufferIdentity(Layer_EditorOverlay);

  DebugDraw::beginFrame();
  for (int i = 0; i < 200; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                        {static_cast<float>(i), 2.0f, 0.0f});
  }
  const bool sceneChanged = DebugDraw::endFrame();

  EXPECT(!sceneChanged,
         "later within-capacity frame should not request another rebuild");
  EXPECT(DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay) ==
             retainedCapacity,
         "within-capacity frame should retain reserved capacity");
  EXPECT(DebugDraw::testing::bufferedVertexCapacity(Layer_EditorOverlay) ==
             retainedCapacity,
         "within-capacity frame should retain padded vertex payload");
  EXPECT(DebugDraw::testing::bufferedIndexCapacity(Layer_EditorOverlay) ==
             retainedCapacity,
         "within-capacity frame should retain padded index payload");
  EXPECT(DebugDraw::testing::vertexBufferIdentity(Layer_EditorOverlay) ==
             retainedVertexIdentity,
         "within-capacity frame should keep vertex buffer identity");
  EXPECT(DebugDraw::testing::indexBufferIdentity(Layer_EditorOverlay) ==
             retainedIndexIdentity,
         "within-capacity frame should keep index buffer identity");
}

void testBucketCapacityDoesNotShrinkAfterSmallerFrame() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  for (int i = 0; i < 129; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                        {static_cast<float>(i), 1.0f, 0.0f});
  }
  DebugDraw::endFrame();
  const usize largeCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);

  DebugDraw::beginFrame();
  DebugDraw::drawLine({0, 0, 0}, {1, 0, 0});
  const bool sceneChanged = DebugDraw::endFrame();

  EXPECT(!sceneChanged,
         "smaller later frame should not request another rebuild");
  EXPECT(DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay) ==
             largeCapacity,
         "smaller later frame should not shrink reserved capacity");
  EXPECT(DebugDraw::testing::bufferedVertexCapacity(Layer_EditorOverlay) ==
             largeCapacity,
         "smaller later frame should retain padded vertex payload");
}

void testEmptyFrameClearsGeometryWithoutShrinkingCapacity() {
  auto scene = makeSceneWithCamera(Layer_All);
  resetForScene(scene);

  DebugDraw::beginFrame();
  for (int i = 0; i < 129; ++i) {
    DebugDraw::drawLine({0.0f, 0.0f, 0.0f},
                        {static_cast<float>(i), 1.0f, 0.0f});
  }
  DebugDraw::endFrame();
  const usize retainedCapacity =
      DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay);
  const auto retainedVertexIdentity =
      DebugDraw::testing::vertexBufferIdentity(Layer_EditorOverlay);
  const auto retainedIndexIdentity =
      DebugDraw::testing::indexBufferIdentity(Layer_EditorOverlay);

  DebugDraw::beginFrame();
  const bool sceneChanged = DebugDraw::endFrame();

  EXPECT(!sceneChanged,
         "empty frame within retained capacity should not request rebuild");
  EXPECT(DebugDraw::testing::flushedVertexCount(Layer_EditorOverlay) == 0,
         "empty frame should clear visible geometry");
  EXPECT(DebugDraw::testing::reservedVertexCapacity(Layer_EditorOverlay) ==
             retainedCapacity,
         "empty frame should retain reserved capacity");
  EXPECT(DebugDraw::testing::bufferedVertexCapacity(Layer_EditorOverlay) ==
             retainedCapacity,
         "empty frame should retain padded vertex payload");
  EXPECT(DebugDraw::testing::bufferedIndexCapacity(Layer_EditorOverlay) ==
             retainedCapacity,
         "empty frame should retain padded index payload");
  EXPECT(DebugDraw::testing::vertexBufferIdentity(Layer_EditorOverlay) ==
             retainedVertexIdentity,
         "empty frame should keep vertex buffer identity");
  EXPECT(DebugDraw::testing::indexBufferIdentity(Layer_EditorOverlay) ==
             retainedIndexIdentity,
         "empty frame should keep index buffer identity");
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

} // namespace

int main() {
  expSetEnvVK();
  initializeRuntimeAssetRoot();

  testDrawLineFlushesSixVertices();
  testDebugDrawBucketDoesNotEnterEditableSceneTree();
  testDebugDrawUsesInjectedMaterialAsset();
  testWireSphereSegmentsMatchThreeGreatCircles();
  testFrustumProducesTwelveLines();
  testOverlayHiddenFromGameCameraMask();
  testOverlayVisibleToEditorCameraMask();
  testBeginFrameClearsPreviousGeometry();
  testBucketCapacityGrowsForLargerLaterFrame();
  testLaterWithinCapacityFrameDoesNotRequestAnotherRebuild();
  testBucketCapacityDoesNotShrinkAfterSmallerFrame();
  testEmptyFrameClearsGeometryWithoutShrinkingCapacity();
  testLineLimitClipsNewestLines();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }

  std::cout << "OK: all debug_draw tests passed\n";
  return 0;
}

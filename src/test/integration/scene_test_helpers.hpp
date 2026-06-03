#pragma once

// Shared helpers for integration tests that need to materialize a
// RenderWorkItem from a Scene. Originally REQ-008; REQ-009 adds a target
// parameter so the queue's scene-level-resource filter can match the
// camera's RenderTarget.

#include "core/rhi/gpu_resource.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/scene.hpp"

#include <cassert>
#include <string>

namespace LX_test {

/// Build a local RenderWorkQueue from `scene` for `pass` + `target` and return
/// the first RenderWorkItem. Asserts the queue is non-empty. Default
inline LX_core::RenderWorkItem
firstItemFromScene(LX_core::Scene &scene, LX_core::StringID pass,
                   const LX_core::RenderTarget &target = {}) {
  LX_core::RenderWorkQueue q;
  q.build(LX_core::RenderWorkBuildContext::realtime(scene), pass, target);
  assert(!q.getItems().empty() &&
         "scene produced no items for pass/target");
  return q.getItems().front();
}

inline LX_core::SceneNodeSharedPtr makeDefaultCameraNodeWithTarget() {
  static int cameraCounter = 0;
  auto node = LX_core::SceneNode::create(
      "test_camera_" + std::to_string(++cameraCounter));
  auto camera = node->addComponent<LX_core::CameraComponent>();
  assert(camera.has_value() && "camera component must attach");
  camera->get().setTarget(LX_core::RenderTarget{});
  camera->get().updateMatrices();
  return node;
}

} // namespace LX_test

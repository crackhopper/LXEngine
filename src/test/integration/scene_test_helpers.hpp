#pragma once

// Shared helpers for integration tests that need to materialize a
// RenderingItem from a Scene. Originally REQ-008; REQ-009 adds a target
// parameter so the queue's scene-level-resource filter can match the
// camera's RenderTarget.

#include "core/rhi/render_resource.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/scene/camera.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/scene.hpp"

#include <cassert>

namespace LX_test {

/// Build a local RenderQueue from `scene` for `pass` + `target` and return
/// the first RenderingItem. Asserts the queue is non-empty. Default
/// `RenderTarget{}` matches the default camera target the Scene constructor
/// sets up (see Scene::Scene(IRenderablePtr)).
inline LX_core::RenderingItem
firstItemFromScene(LX_core::Scene &scene, LX_core::StringID pass,
                   const LX_core::RenderTarget &target = {}) {
  LX_core::RenderQueue q;
  q.buildFromScene(scene, pass, target);
  assert(!q.getItems().empty() &&
         "scene produced no items for pass/target");
  return q.getItems().front();
}

/// Construct a default Camera (Forward pass) whose m_target is explicitly set
/// to a default-constructed RenderTarget. Use this in test setup after the
/// legacy Scene ctor's auto-camera stops being created (task 7).
inline LX_core::CameraPtr makeDefaultCameraWithTarget() {
  auto cam =
      std::make_shared<LX_core::Camera>(LX_core::ResourcePassFlag::Forward);
  cam->setTarget(LX_core::RenderTarget{});
  return cam;
}

} // namespace LX_test

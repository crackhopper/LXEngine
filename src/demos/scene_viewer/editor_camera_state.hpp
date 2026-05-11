#pragma once

#include "core/math/vec.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"

namespace LX_demo::scene_viewer {

struct EditorCameraState final {
  LX_core::Vec3f position{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f rotationEulerDeg{0.0f, 0.0f, 0.0f};
  float fovY = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;

  [[nodiscard]] static EditorCameraState
  captureFrom(const LX_core::SceneNode& node,
              const LX_core::CameraComponent& camera);

  void applyTo(LX_core::SceneNode& node,
               LX_core::CameraComponent& camera) const;
  void applyToNode(LX_core::SceneNode& node) const;
  void applyToCamera(LX_core::CameraComponent& camera) const;
};

} // namespace LX_demo::scene_viewer

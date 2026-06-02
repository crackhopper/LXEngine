#pragma once

#include "core/frame_graph/render_target.hpp"
#include "core/math/bounds.hpp"
#include "core/math/ray.hpp"
#include "core/math/transform.hpp"
#include "core/platform/types.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/component.hpp"
#include "core/scene/scene_resource_table.hpp"

#include <optional>

namespace LX_core {

class CameraComponent final : public IComponent {
public:
  CameraComponent() = default;

  ComponentTypeId getTypeId() const override {
    return componentTypeId<CameraComponent>();
  }

  CameraDataSharedPtr getUBO() const { return m_ubo; }
  [[nodiscard]] CameraHandle getCameraHandle() const { return m_cameraHandle; }
  void setCameraHandle(CameraHandle handle) { m_cameraHandle = handle; }

  [[nodiscard]] CameraType getProjectionType() const { return m_type; }
  void setProjectionType(CameraType projectionType);

  [[nodiscard]] float getFovY() const { return m_fovY; }
  void setFovY(float value);

  [[nodiscard]] float getAspect() const { return m_aspect; }
  void setAspect(float value);

  [[nodiscard]] float getNearPlane() const { return m_nearPlane; }
  void setNearPlane(float value);

  [[nodiscard]] float getFarPlane() const { return m_farPlane; }
  void setFarPlane(float value);

  [[nodiscard]] float getLeft() const { return m_left; }
  [[nodiscard]] float getRight() const { return m_right; }
  [[nodiscard]] float getBottom() const { return m_bottom; }
  [[nodiscard]] float getTop() const { return m_top; }
  void setOrthographicBounds(float leftValue, float rightValue,
                             float bottomValue, float topValue);
  void applyProjectionState(CameraType projectionType, float fovY, float aspect,
                            float nearPlane, float farPlane, float leftValue,
                            float rightValue, float bottomValue,
                            float topValue);

  const std::optional<RenderTarget> &getTarget() const { return m_target; }
  void setTarget(RenderTarget target);
  void setTarget(std::optional<RenderTarget> target);
  void clearTarget();
  bool matchesTarget(const RenderTarget &target) const {
    return m_target.has_value() && *m_target == target;
  }

  VisibilityLayerMask getCullingMask() const { return m_cullingMask; }
  void setCullingMask(VisibilityLayerMask mask);

  bool isActive() const { return m_active; }
  void setActive(bool active);

  Vec3f getEyePosition() const;
  Vec3f getForwardVector() const;
  Vec3f getUpVector() const;
  Vec3f getLookTarget(float distance = 1.0f) const;
  BoundingBox getDebugLocalBounds() const;

  Mat4f getViewMatrix() const;
  Mat4f getProjMatrix(float aspectOverride = 0.0f,
                      GraphicsAPI api = GraphicsAPI::Vulkan) const;
  Ray pickRay(const Vec2f &screenPixel, const Vec2f &viewportSize) const;
  void updateMatrices();

  void setPosition(const Vec3f &position);
  void lookAt(const Vec3f &eye, const Vec3f &target, const Vec3f &up);

private:
  Transform getOwnerWorldTransform() const;

  CameraType m_type = CameraType::Perspective;
  float m_fovY = 45.0f;
  float m_aspect = 16.0f / 9.0f;
  float m_nearPlane = 0.1f;
  float m_farPlane = 1000.0f;
  float m_left = -1.0f;
  float m_right = 1.0f;
  float m_bottom = -1.0f;
  float m_top = 1.0f;
  CameraDataSharedPtr m_ubo = std::make_shared<CameraData>();
  std::optional<float> m_lookDistance;
  std::optional<RenderTarget> m_target;
  VisibilityLayerMask m_cullingMask = Layer_All & ~Layer_EditorOverlay;
  bool m_active = true;
  CameraHandle m_cameraHandle;
};

} // namespace LX_core

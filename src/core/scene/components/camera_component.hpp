#pragma once

#include "core/frame_graph/render_target.hpp"
#include "core/math/ray.hpp"
#include "core/math/transform.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/component.hpp"

#include <optional>

namespace LX_core {

class CameraComponent final : public IComponent {
public:
  CameraComponent() = default;

  ComponentTypeId getTypeId() const override {
    return componentTypeId<CameraComponent>();
  }

  CameraDataSharedPtr getUBO() const { return m_ubo; }

  CameraType type = CameraType::Perspective;
  float fovY = 45.0f;
  float aspect = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  float left = -1.0f;
  float right = 1.0f;
  float bottom = -1.0f;
  float top = 1.0f;

  const std::optional<RenderTarget> &getTarget() const { return m_target; }
  void setTarget(RenderTarget target) { m_target = std::move(target); }
  void setTarget(std::optional<RenderTarget> target) { m_target = std::move(target); }
  void clearTarget() { m_target.reset(); }
  bool matchesTarget(const RenderTarget &target) const {
    return m_target.has_value() && *m_target == target;
  }

  VisibilityLayerMask getCullingMask() const { return m_cullingMask; }
  void setCullingMask(VisibilityLayerMask mask) { m_cullingMask = mask; }

  bool isActive() const { return m_active; }
  void setActive(bool active) { m_active = active; }

  Vec3f getEyePosition() const;
  Vec3f getForwardVector() const;
  Vec3f getUpVector() const;
  Vec3f getLookTarget(float distance = 1.0f) const;

  Mat4f getViewMatrix() const;
  Mat4f getProjMatrix(float aspectOverride = 0.0f) const;
  Ray pickRay(const Vec2f &screenPixel, const Vec2f &viewportSize) const;
  void updateMatrices();

  void setPosition(const Vec3f &position);
  void lookAt(const Vec3f &eye, const Vec3f &target, const Vec3f &up);

private:
  Transform getOwnerWorldTransform() const;

  CameraDataSharedPtr m_ubo = std::make_shared<CameraData>();
  std::optional<float> m_lookDistance;
  std::optional<RenderTarget> m_target;
  VisibilityLayerMask m_cullingMask = Layer_All & ~Layer_EditorOverlay;
  bool m_active = true;
};

} // namespace LX_core

#pragma once
#include "core/frame_graph/pass.hpp"
#include "core/math/bounds.hpp"
#include "core/math/mat.hpp"
#include "core/math/vec.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/utils/string_table.hpp"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace LX_core {

class Scene;
class SceneNode;
class CameraComponent;

/// Abstract base for all light types. A concrete light contributes (a) pass
/// participation rules owned by the light itself and (b) an optional data
/// resource to feed shaders. Runtime filtering in
/// RenderWorkQueue::build / Scene::getSceneLevelResources goes through
/// this interface.
class LightBase {
public:
  virtual ~LightBase() = default;

  /// The light's GPU-side data resource, or an empty ref if the light
  /// contributes no per-frame descriptor data. Binding name is owned by the
  /// resource itself via IGpuResource::getBindingName().
  virtual GpuResourceRef getUBO() const = 0;
  [[nodiscard]] virtual std::unique_ptr<LightBase> cloneUnique() const = 0;

  /// Whether this light participates in the given pass.
  virtual bool supportsPass(StringID pass) const = 0;
  virtual void attachToSceneNode(const std::weak_ptr<Scene> &scene,
                                 const std::weak_ptr<SceneNode> &node) = 0;
  virtual void detachFromSceneNode() = 0;
  [[nodiscard]] virtual std::shared_ptr<SceneNode> getSceneNode() const = 0;

  /// Editor/debug pick bounds in the owning SceneNode's local space. Lights do
  /// not render mesh geometry, but the editor can use this to draw and pick the
  /// light through the owning node instead of adding runtime helper children.
  virtual BoundingBox getDebugLocalBounds() const { return {}; }
};

using LightBaseSharedPtr = std::shared_ptr<LightBase>;

inline constexpr u32 MaxShadowCascades = 4;

struct alignas(16) DirectionalLightData : public IGpuResource {
  explicit DirectionalLightData(StringID bindingName = StringID("LightUBO"))
      : m_bindingName(bindingName) {}
  struct Param {
    Vec4f dir;
    Vec4f color;
    Mat4f shadowViewProj;
    Mat4f cascadeViewProj[MaxShadowCascades];
    Vec4f cascadeSplits;
    Vec4f cascadeDepthRanges;
    Vec4f shadowParams;
  };
  Param param;
  static constexpr u32 ResourceSize = sizeof(Param);

  virtual ResourceType getType() const override {
    return ResourceType::UniformBuffer;
  }
  virtual const void *getRawData() const override { return &param; }
  virtual u32 getByteSize() const override { return ResourceSize; }

  StringID getBindingName() const override { return m_bindingName; }

private:
  StringID m_bindingName;
};
using DirectionalLightDataUniquePtr = std::unique_ptr<DirectionalLightData>;

struct DirectionalShadowCascadeDebugView final {
  Vec3f eye;
  Vec3f target;
  Vec3f up;
  float left = -1.0f;
  float right = 1.0f;
  float bottom = -1.0f;
  float top = 1.0f;
  float nearPlane = 0.1f;
  float farPlane = 100.0f;
};

inline constexpr u32 MaxDirectionalLights = 4;
inline constexpr u32 MaxPointLights = 16;
inline constexpr u32 MaxSpotLights = 8;

struct alignas(16) SceneLightsData : public IGpuResource {
  explicit SceneLightsData(StringID bindingName = StringID("SceneLightsUBO"))
      : m_bindingName(bindingName) {}

  struct DirectionalEntry {
    Vec4f direction;
    Vec4f colorIntensity;
  };

  struct PointEntry {
    Vec4f positionRange;
    Vec4f colorIntensity;
  };

  struct SpotEntry {
    Vec4f positionRange;
    Vec4f directionCone;
    Vec4f colorIntensity;
  };

  struct Param {
    Vec4i counts;
    DirectionalEntry directional[MaxDirectionalLights];
    PointEntry point[MaxPointLights];
    SpotEntry spot[MaxSpotLights];
  };

  Param param{};
  static constexpr u32 ResourceSize = sizeof(Param);

  ResourceType getType() const override { return ResourceType::UniformBuffer; }
  const void *getRawData() const override { return &param; }
  u32 getByteSize() const override { return ResourceSize; }
  StringID getBindingName() const override { return m_bindingName; }

private:
  StringID m_bindingName;
};

using SceneLightsDataUniquePtr = std::unique_ptr<SceneLightsData>;

class DirectionalLight : public LightBase {
public:
  /// Default supported passes: Forward + Deferred + Shadow. The first
  /// directional light acts as the v0.1.1 main shadow caster.
  DirectionalLight();

  [[nodiscard]] Vec3f getDirection() const;
  [[nodiscard]] Vec3f getColor() const;
  [[nodiscard]] float getIntensity() const;
  [[nodiscard]] Mat4f getShadowViewProj() const;
  [[nodiscard]] Vec4f getShadowParams() const;
  [[nodiscard]] float getShadowDistance() const;
  [[nodiscard]] Vec4f getCascadeSplits() const;
  [[nodiscard]] u32 getShadowCascadeCount() const;
  [[nodiscard]] std::shared_ptr<SceneNode> getSceneNode() const override;
  void setDirection(const Vec3f &direction);
  void setColor(const Vec3f &color);
  void setIntensity(float intensity);
  void setShadowMapSize(float size);
  void setShadowBias(float bias);
  void setShadowStrength(float strength);
  void setShadowCascadeCount(u32 count);
  void setShadowDistance(float distance);
  void updateShadowCascadesForCamera(const CameraComponent &camera,
                                     float splitLambda = 0.5f);
  [[nodiscard]] std::optional<DirectionalShadowCascadeDebugView>
  getShadowCascadeDebugView(u32 cascadeIndex) const;
  [[nodiscard]] DirectionalLightDataUniquePtr
  makeShadowCascadeUBOSnapshot(u32 cascadeIndex) const;
  void setActiveShadowCascade(u32 cascadeIndex);

  void attachToSceneNode(const std::weak_ptr<Scene> &scene,
                         const std::weak_ptr<SceneNode> &node) override;
  void detachFromSceneNode() override;

  GpuResourceRef getUBO() const override { return GpuResourceRef{*m_ubo}; }
  [[nodiscard]] std::unique_ptr<LightBase> cloneUnique() const override;
  [[nodiscard]] DirectionalLightData &getDirectionalUBO() { return *m_ubo; }
  [[nodiscard]] const DirectionalLightData &getDirectionalUBO() const {
    return *m_ubo;
  }
  bool supportsPass(StringID pass) const override;
  BoundingBox getDebugLocalBounds() const override;
  void setSupportedPasses(std::initializer_list<StringID> passes);
  void setSupportedPasses(const std::vector<StringID> &passes);

private:
  void updateShadowViewProjection();
  void emitLightPropertyChanged() const;

  DirectionalLightDataUniquePtr m_ubo;
  Vec3f m_pendingDirection{0.35f, -1.0f, 0.25f};
  DirectionalShadowCascadeDebugView
      m_shadowCascadeDebugViews[MaxShadowCascades];
  bool m_shadowCascadeDebugViewValid[MaxShadowCascades] = {};
  float m_shadowDistance = 80.0f;
  std::unordered_set<StringID, StringID::Hash> m_supportedPasses;
  std::weak_ptr<Scene> m_scene;
  std::weak_ptr<SceneNode> m_node;
};
using DirectionalLightSharedPtr = std::shared_ptr<DirectionalLight>;

class PointLight final : public LightBase {
public:
  PointLight();

  [[nodiscard]] Vec3f getColor() const;
  [[nodiscard]] float getIntensity() const;
  [[nodiscard]] float getRange() const;
  [[nodiscard]] std::shared_ptr<SceneNode> getSceneNode() const override;
  void setColor(const Vec3f &color);
  void setIntensity(float intensity);
  void setRange(float range);

  void attachToSceneNode(const std::weak_ptr<Scene> &scene,
                         const std::weak_ptr<SceneNode> &node) override;
  void detachFromSceneNode() override;

  GpuResourceRef getUBO() const override { return {}; }
  [[nodiscard]] std::unique_ptr<LightBase> cloneUnique() const override;
  bool supportsPass(StringID pass) const override;
  BoundingBox getDebugLocalBounds() const override;
  void setSupportedPasses(std::initializer_list<StringID> passes);
  void setSupportedPasses(const std::vector<StringID> &passes);

private:
  void emitLightPropertyChanged() const;

  Vec3f m_color{1.0f, 0.98f, 0.9f};
  float m_intensity = 1.0f;
  float m_range = 5.0f;
  std::unordered_set<StringID, StringID::Hash> m_supportedPasses;
  std::weak_ptr<Scene> m_scene;
  std::weak_ptr<SceneNode> m_node;
};

using PointLightSharedPtr = std::shared_ptr<PointLight>;

class SpotLight final : public LightBase {
public:
  SpotLight();

  [[nodiscard]] Vec3f getDirection() const;
  [[nodiscard]] Vec3f getColor() const;
  [[nodiscard]] float getIntensity() const;
  [[nodiscard]] float getRange() const;
  [[nodiscard]] float getInnerConeDegrees() const;
  [[nodiscard]] float getOuterConeDegrees() const;
  [[nodiscard]] std::shared_ptr<SceneNode> getSceneNode() const override;
  void setDirection(const Vec3f &direction);
  void setColor(const Vec3f &color);
  void setIntensity(float intensity);
  void setRange(float range);
  void setInnerConeDegrees(float degrees);
  void setOuterConeDegrees(float degrees);

  void attachToSceneNode(const std::weak_ptr<Scene> &scene,
                         const std::weak_ptr<SceneNode> &node) override;
  void detachFromSceneNode() override;

  GpuResourceRef getUBO() const override { return {}; }
  [[nodiscard]] std::unique_ptr<LightBase> cloneUnique() const override;
  bool supportsPass(StringID pass) const override;
  BoundingBox getDebugLocalBounds() const override;
  void setSupportedPasses(std::initializer_list<StringID> passes);
  void setSupportedPasses(const std::vector<StringID> &passes);

private:
  void emitLightPropertyChanged() const;

  Vec3f m_direction{0.0f, -1.0f, 0.0f};
  Vec3f m_color{1.0f, 0.98f, 0.9f};
  float m_intensity = 1.0f;
  float m_range = 8.0f;
  float m_innerConeDegrees = 20.0f;
  float m_outerConeDegrees = 35.0f;
  std::unordered_set<StringID, StringID::Hash> m_supportedPasses;
  std::weak_ptr<Scene> m_scene;
  std::weak_ptr<SceneNode> m_node;
};

using SpotLightSharedPtr = std::shared_ptr<SpotLight>;

} // namespace LX_core

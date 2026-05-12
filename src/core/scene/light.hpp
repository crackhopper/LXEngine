#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/math/vec.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/string_table.hpp"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <unordered_set>
#include <vector>

namespace LX_core {

class Scene;
class SceneNode;

/// Abstract base for all light types. A concrete light contributes (a) pass
/// participation rules owned by the light itself and (b) an optional data
/// resource to feed shaders. Runtime filtering in
/// RenderQueue::buildFromScene / Scene::getSceneLevelResources goes through
/// this interface.
class LightBase {
public:
  virtual ~LightBase() = default;

  /// The light's GPU-side data resource, or nullptr if the light contributes no
  /// per-frame descriptor data. Binding name is owned by the resource itself
  /// via IGpuResource::getBindingName().
  virtual IGpuResourceSharedPtr getUBO() const = 0;

  /// Whether this light participates in the given pass.
  virtual bool supportsPass(StringID pass) const = 0;
};

using LightBaseSharedPtr = std::shared_ptr<LightBase>;

struct alignas(16) DirectionalLightData : public IGpuResource {
  explicit DirectionalLightData(StringID bindingName = StringID("LightUBO"))
      : m_bindingName(bindingName) {}
  struct Param {
    Vec4f dir;
    Vec4f color;
  };
  Param param;
  static constexpr u32 ResourceSize = sizeof(Param);

  virtual ResourceType getType() const override {
    return ResourceType::UniformBuffer;
  }
  virtual const void *getRawData() const override { return &param; }
  virtual u32 getByteSize() const override {
    return ResourceSize;
  }

  StringID getBindingName() const override {
    return m_bindingName;
  }

private:
  StringID m_bindingName;
};
using DirectionalLightDataSharedPtr = std::shared_ptr<DirectionalLightData>;

class DirectionalLight : public LightBase {
public:
  /// Default supported passes: Forward + Deferred. Shadow participation is
  /// opt-in because a directional light only writes the shadow map when
  /// explicitly configured as a shadow caster.
  DirectionalLight();

  [[nodiscard]] Vec3f getDirection() const;
  [[nodiscard]] Vec3f getColor() const;
  [[nodiscard]] float getIntensity() const;
  void setDirection(const Vec3f &direction);
  void setColor(const Vec3f &color);
  void setIntensity(float intensity);

  void attachToSceneNode(const std::weak_ptr<Scene> &scene,
                         const std::weak_ptr<SceneNode> &node);
  void detachFromSceneNode();

  IGpuResourceSharedPtr getUBO() const override { return m_ubo; }
  [[nodiscard]] DirectionalLightDataSharedPtr getDirectionalUBO() const {
    return m_ubo;
  }
  bool supportsPass(StringID pass) const override;
  void setSupportedPasses(std::initializer_list<StringID> passes);
  void setSupportedPasses(const std::vector<StringID> &passes);

private:
  void emitLightPropertyChanged() const;

  DirectionalLightDataSharedPtr m_ubo;
  std::unordered_set<StringID, StringID::Hash> m_supportedPasses;
  std::weak_ptr<Scene> m_scene;
  std::weak_ptr<SceneNode> m_node;
};
using DirectionalLightSharedPtr = std::shared_ptr<DirectionalLight>;

} // namespace LX_core

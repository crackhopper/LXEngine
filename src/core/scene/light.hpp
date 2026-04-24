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
  static constexpr ResourceByteSize32 ResourceSize = sizeof(Param);

  virtual ResourceType getType() const override {
    return ResourceType::UniformBuffer;
  }
  virtual const void *getRawData() const override { return &param; }
  virtual ResourceByteSize32 getByteSize() const override {
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
  DirectionalLight()
      : ubo(std::make_shared<DirectionalLightData>()),
        m_supportedPasses({Pass_Forward, Pass_Deferred}) {}

  /// Direct access to the strongly-typed light data (legacy callers mutate
  /// `ubo->param` directly; new callers go through LightBase::getUBO()).
  DirectionalLightDataSharedPtr ubo;

  IGpuResourceSharedPtr getUBO() const override { return ubo; }
  bool supportsPass(StringID pass) const override {
    return m_supportedPasses.find(pass) != m_supportedPasses.end();
  }
  void setSupportedPasses(std::initializer_list<StringID> passes) {
    m_supportedPasses = {passes.begin(), passes.end()};
  }
  void setSupportedPasses(const std::vector<StringID> &passes) {
    m_supportedPasses = {passes.begin(), passes.end()};
  }

private:
  std::unordered_set<StringID, StringID::Hash> m_supportedPasses;
};
using DirectionalLightSharedPtr = std::shared_ptr<DirectionalLight>;

} // namespace LX_core

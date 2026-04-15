#pragma once
#include "core/rhi/render_resource.hpp"
#include "core/math/vec.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/utils/string_table.hpp"

#include <cstdint>
#include <memory>

namespace LX_core {

/// Abstract base for all light types. A concrete light contributes (a) a
/// pass mask describing which rendering passes it participates in and
/// (b) an optional UBO resource to feed shaders. Runtime filtering in
/// RenderQueue::buildFromScene / Scene::getSceneLevelResources goes through
/// this interface.
class LightBase {
public:
  virtual ~LightBase() = default;

  /// Which passes this light participates in.
  virtual ResourcePassFlag getPassMask() const = 0;

  /// The light's GPU-side resource, or nullptr if the light contributes no
  /// per-frame descriptor data. Binding name is owned by the resource itself
  /// via IRenderResource::getBindingName().
  virtual IRenderResourcePtr getUBO() const = 0;

  /// Whether this light participates in the given pass. Default
  /// implementation bitwise-tests getPassMask() against
  /// passFlagFromStringID(pass); subclasses may override for richer rules.
  virtual bool supportsPass(StringID pass) const {
    const auto flag = passFlagFromStringID(pass);
    return (static_cast<std::uint32_t>(getPassMask()) &
            static_cast<std::uint32_t>(flag)) != 0;
  }
};

using LightBasePtr = std::shared_ptr<LightBase>;

struct alignas(16) DirectionalLightUBO : public IRenderResource {
  DirectionalLightUBO(ResourcePassFlag passFlag) : m_passFlag(passFlag) {}
  struct Param {
    Vec4f dir;
    Vec4f color;
  };
  Param param;
  static constexpr usize ResourceSize = sizeof(Param);

  virtual ResourcePassFlag getPassFlag() const override { return m_passFlag; }
  virtual ResourceType getType() const override {
    return ResourceType::UniformBuffer;
  }
  virtual const void *getRawData() const override { return &param; }
  virtual u32 getByteSize() const override { return ResourceSize; }

  StringID getBindingName() const override {
    static const StringID kName("LightUBO");
    return kName;
  }

private:
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};
using DirectionalLightUboPtr = std::shared_ptr<DirectionalLightUBO>;

class DirectionalLight : public LightBase {
public:
  /// Default pass mask: Forward + Deferred. Shadow participation is opt-in
  /// because a directional light only writes the shadow map when explicitly
  /// configured as a shadow caster.
  DirectionalLight(ResourcePassFlag passFlag = ResourcePassFlag::Forward,
                   ResourcePassFlag passMask = ResourcePassFlag::Forward |
                                               ResourcePassFlag::Deferred)
      : ubo(std::make_shared<DirectionalLightUBO>(passFlag)),
        m_passMask(passMask) {}

  /// Direct access to the strongly-typed UBO (legacy callers mutate
  /// `ubo->param` directly; new callers go through LightBase::getUBO()).
  DirectionalLightUboPtr ubo;

  ResourcePassFlag getPassMask() const override { return m_passMask; }
  IRenderResourcePtr getUBO() const override { return ubo; }

  void setPassMask(ResourcePassFlag mask) { m_passMask = mask; }

private:
  ResourcePassFlag m_passMask;
};
using DirectionalLightPtr = std::shared_ptr<DirectionalLight>;

} // namespace LX_core

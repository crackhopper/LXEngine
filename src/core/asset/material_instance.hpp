#pragma once

#include "core/asset/material_template.hpp"
#include "core/asset/parameter_buffer.hpp"
#include "core/asset/texture.hpp"
#include "core/math/vec.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LX_core {

/*
@source_analysis.section MaterialInstance：模板的运行时账本，而不是第二份模板
如果说 `MaterialTemplate` 像蓝图，`MaterialInstance` 更像一本运行时账本：
它不重新定义 pass 结构，不持有 shader 编译逻辑，也不决定 pipeline 身份；
它负责记录“这次绘制具体要用什么参数、什么纹理、哪些 pass 处于启用状态”。

这个类里最容易看懂的主线有三条：

1. 构造期：按 template 的 canonical material bindings 建立运行时数据表
2. 写入期：按 binding/member 把值写进 buffer binding data，并标记 dirty
3. 读取期：按 pass 视角从 canonical 资源集合里筛选 descriptor resources

所以它本质上是 template 和 backend 之间的一层“实例态翻译层”。
*/
class MaterialInstance {
  struct Token {};

public:
  using SharedPtr = std::shared_ptr<MaterialInstance>;

  MaterialInstance(Token, MaterialTemplateSharedPtr tmpl);

  static SharedPtr create(MaterialTemplateSharedPtr tmpl) {
    return std::make_shared<MaterialInstance>(Token{}, std::move(tmpl));
  }

  MaterialInstance(const MaterialInstance &) = delete;
  MaterialInstance &operator=(const MaterialInstance &) = delete;
  MaterialInstance(MaterialInstance &&) = delete;
  MaterialInstance &operator=(MaterialInstance &&) = delete;

  std::vector<IGpuResourceSharedPtr>
  getDescriptorResources(StringID pass) const;
  IShaderSharedPtr getPassShader(StringID pass) const;
  RenderState getPassRenderState(StringID pass) const;
  StringID getMaterialSignature(StringID pass) const;

  // Primary API: write buffer parameter by binding name + member name.
  void setParameter(StringID bindingName, StringID memberName, float value);
  void setParameter(StringID bindingName, StringID memberName, int32_t value);
  void setParameter(StringID bindingName, StringID memberName,
                    const Vec3f &value);
  void setParameter(StringID bindingName, StringID memberName,
                    const Vec4f &value);

  void setTexture(StringID bindingName, CombinedTextureSamplerSharedPtr tex);

  void syncGpuData();

  MaterialTemplateSharedPtr getTemplate() const { return m_template; }

  // Multi-buffer accessors.
  size_t getParameterBufferCount() const {
    return m_parameterBuffersByName.size();
  }
  const std::vector<uint8_t> &
  getParameterBufferBytes(StringID bindingName) const;
  const ShaderResourceBinding *
  getParameterBufferLayout(StringID bindingName) const;
  // Single-binding shortcuts (assert if multiple buffer bindings exist).
  const std::vector<uint8_t> &getParameterBufferBytes() const;
  const ShaderResourceBinding *getParameterBufferLayout() const;

  bool isPassEnabled(StringID pass) const;
  void setPassEnabled(StringID pass, bool enabled);
  std::vector<StringID> getEnabledPasses() const;
  uint64_t addPassStateListener(std::function<void()> callback);
  void removePassStateListener(uint64_t listenerId);

private:
  ParameterBuffer *findParameterBuffer(StringID bindingName);
  const ParameterBuffer *findParameterBuffer(StringID bindingName) const;
  bool hasDefinedPass(StringID pass) const;

  MaterialTemplateSharedPtr m_template;
  // Runtime resources grouped by the same binding names used by the template's
  // canonical ShaderResourceBinding table.
  std::unordered_map<StringID, ParameterBufferSharedPtr, StringID::Hash>
      m_parameterBuffersByName;

  // Runtime sampled-image resources keyed by the same canonical binding names
  // as the template's texture bindings.
  std::unordered_map<StringID, CombinedTextureSamplerSharedPtr, StringID::Hash>
      m_textureBindingsByName;
  // Structural pass participation state. This changes scene validation,
  // unlike ordinary parameter writes.
  std::unordered_set<StringID, StringID::Hash> m_enabledPasses;
  std::unordered_map<uint64_t, std::function<void()>> m_passStateListeners;
  uint64_t m_nextListenerId = 1;
};

using MaterialInstanceSharedPtr = MaterialInstance::SharedPtr;

} // namespace LX_core

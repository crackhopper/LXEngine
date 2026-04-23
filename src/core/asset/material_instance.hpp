#pragma once

#include "core/asset/material_template.hpp"
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
@source_analysis.section MaterialParameterData：一个完整的材质参数槽位
这个类型现在不再只是一个 `IGpuResource` wrapper，而是一个完整的
material-owned buffer binding 运行时对象：

- 持有 `bindingName`
- 持有 `binding`（反射布局）
- 持有 `buffer`（CPU 字节）
- 自己实现 `IGpuResource`
- 自己记录 `dirty`（等待上传）

也就是说，过去 `MaterialBufferSlot + MaterialParameterData` 两层共同表达的概念，
现在收敛成一个类：一个“可写、可查布局、可交给 backend”的材质参数槽位。

这样做的直接收益是，写路径和读路径面对的是同一个对象：

- `setParameter(...)` 直接写 `MaterialParameterData::buffer`
- `getDescriptorResources(pass)` 直接返回这个对象本身
- `syncGpuData()` 直接把这个对象标记为 backend dirty

相比“旧参数容器再间接引用原始 buffer”的设计，
这里去掉了中间层，也去掉了 non-owning buffer 指针带来的生命周期约束。
*/
class MaterialParameterData : public IGpuResource {
public:
  MaterialParameterData(StringID bindingName, const ShaderResourceBinding &binding,
                        ResourceType resType = ResourceType::UniformBuffer);

  ResourceType getType() const override { return m_resType; }
  const void *getRawData() const override { return m_buffer.data(); }
  u32 getByteSize() const override {
    return static_cast<u32>(m_buffer.size());
  }
  StringID getBindingName() const override { return m_bindingName; }

  const ShaderResourceBinding &getBinding() const { return m_binding.get(); }
  const std::vector<uint8_t> &getBuffer() const { return m_buffer; }

  bool hasPendingSync() const { return m_dirty; }
  void clearPendingSync() { m_dirty = false; }

  void writeParameterMember(StringID memberName, const void *src, size_t nbytes,
                            ShaderPropertyType expected);

private:
  StringID m_bindingName;
  std::reference_wrapper<const ShaderResourceBinding> m_binding;
  std::vector<uint8_t> m_buffer;
  ResourceType m_resType;
  bool m_dirty = false;
};

/*
@source_analysis.section MaterialInstance：模板的运行时账本，而不是第二份模板
如果说 `MaterialTemplate` 像蓝图，`MaterialInstance` 更像一本运行时账本：
它不重新定义 pass 结构，不持有 shader 编译逻辑，也不决定 pipeline 身份；
它负责记录“这次绘制具体要用什么参数、什么纹理、哪些 pass 处于启用状态”。

这个类里最容易看懂的主线有三条：

1. 构造期：按 template 的 canonical material bindings 建立运行时数据表
2. 写入期：按 binding/member 把值写进 parameter data，并标记 dirty
3. 读取期：按 pass 视角从 canonical 资源集合里筛选 descriptor resources

所以它本质上是 template 和 backend 之间的一层“实例态翻译层”。
*/
class MaterialInstance {
  struct Token {};

public:
  using Ptr = std::shared_ptr<MaterialInstance>;

  MaterialInstance(Token, MaterialTemplate::Ptr tmpl);

  static Ptr create(MaterialTemplate::Ptr tmpl) {
    return std::make_shared<MaterialInstance>(Token{}, std::move(tmpl));
  }

  MaterialInstance(const MaterialInstance &) = delete;
  MaterialInstance &operator=(const MaterialInstance &) = delete;
  MaterialInstance(MaterialInstance &&) = delete;
  MaterialInstance &operator=(MaterialInstance &&) = delete;

  std::vector<IGpuResourcePtr> getDescriptorResources(StringID pass) const;
  IShaderPtr getPassShader(StringID pass) const;
  RenderState getPassRenderState(StringID pass) const;
  StringID getMaterialSignature(StringID pass) const;

  // Primary API: write buffer parameter by binding name + member name.
  void setParameter(StringID bindingName, StringID memberName, float value);
  void setParameter(StringID bindingName, StringID memberName, int32_t value);
  void setParameter(StringID bindingName, StringID memberName,
                    const Vec3f &value);
  void setParameter(StringID bindingName, StringID memberName,
                    const Vec4f &value);

  void setTexture(StringID bindingName, CombinedTextureSamplerPtr tex);

  void syncGpuData();

  MaterialTemplate::Ptr getTemplate() const { return m_template; }

  // Multi-buffer accessors.
  size_t getParameterBindingCount() const {
    return m_parameterDataByBinding.size();
  }
  const std::vector<uint8_t> &getParameterBuffer(StringID bindingName) const;
  const ShaderResourceBinding *getParameterBinding(StringID bindingName) const;
  // Single-binding shortcuts (assert if multiple parameter bindings exist).
  const std::vector<uint8_t> &getParameterBuffer() const;
  const ShaderResourceBinding *getParameterBinding() const;

  bool isPassEnabled(StringID pass) const;
  void setPassEnabled(StringID pass, bool enabled);
  std::vector<StringID> getEnabledPasses() const;
  uint64_t addPassStateListener(std::function<void()> callback);
  void removePassStateListener(uint64_t listenerId);

private:
  MaterialParameterData *findParameterDataByBinding(StringID bindingName);
  const MaterialParameterData *
  findParameterDataByBinding(StringID bindingName) const;
  bool hasDefinedPass(StringID pass) const;

  MaterialTemplate::Ptr m_template;
  // Canonical per-binding runtime buffer data, keyed by the template's
  // canonical material-owned binding ids.
  std::unordered_map<StringID, std::shared_ptr<MaterialParameterData>, StringID::Hash>
      m_parameterDataByBinding;

  // Canonical textures keyed by shader-declared binding name.
  std::unordered_map<StringID, CombinedTextureSamplerPtr> m_textureBindings;
  // Structural pass participation state. This changes scene validation,
  // unlike ordinary parameter writes.
  std::unordered_set<StringID, StringID::Hash> m_enabledPasses;
  std::unordered_map<uint64_t, std::function<void()>> m_passStateListeners;
  uint64_t m_nextListenerId = 1;
};

using MaterialInstancePtr = MaterialInstance::Ptr;

} // namespace LX_core

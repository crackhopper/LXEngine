#pragma once

#include "core/asset/shader.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace LX_core {

/*
@source_analysis.section ParameterBuffer：材质实例里一份可写的 buffer 绑定资源
`ParameterBuffer` 和 `CombinedTextureSampler` 现在是 `MaterialInstance` 持有的两类并列运行时资源。
它不再只是“参数字节数组”，而是一份完整的 buffer-type binding 运行时对象：

- 对应 `MaterialTemplate` canonical material bindings 里的一个 `ShaderResourceBinding`
- 持有 CPU 侧字节数据，供 `setParameter(...)` 按 member 写入
- 直接实现 `IGpuResource`，让 backend 能按统一资源路径上传

当前它覆盖所有 buffer-type material-owned binding，也就是 `UniformBuffer` 和
`StorageBuffer`；纹理类 binding 则继续由 `CombinedTextureSampler` 表达。
*/
class ParameterBuffer : public IGpuResource {
public:
  ParameterBuffer(StringID bindingName, const ShaderResourceBinding &binding,
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

  void writeBindingMember(StringID memberName, const void *src, size_t nbytes,
                          ShaderPropertyType expected);

private:
  StringID m_bindingName;
  std::reference_wrapper<const ShaderResourceBinding> m_binding;
  std::vector<uint8_t> m_buffer;
  ResourceType m_resType;
  bool m_dirty = false;
};

using ParameterBufferSharedPtr = std::shared_ptr<ParameterBuffer>;

} // namespace LX_core

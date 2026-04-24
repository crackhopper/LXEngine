#pragma once
#include "core/math/mat.hpp"
#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/utils/string_table.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace LX_core {
// 资源槽位类型，后端根据这个走不同的处理流程。
enum class ResourceType : u8 {
  None = 0,
  VertexBuffer,
  IndexBuffer,
  UniformBuffer,
  StorageBuffer,
  CombinedImageSampler,
  Special,
  Count
};

using ResourceCacheIdentity = uint64_t;

/*
@source_analysis.section IGpuResource：core 层定义的“可被 GPU 消费”的统一契约
这个接口不是 Vulkan buffer / image 的后端对象，而是 core 层给 backend 的统一入口：
只要某个对象能提供“资源类型 + 原始字节 + 字节大小 + 可选 binding 名”，
backend 就可以沿同一条同步和绑定路径处理它。

这也是为什么项目里很多业务类型会直接实现它：

- `VertexBuffer` / `IndexBuffer`：把几何数据暴露给 upload 路径
- `CameraData` / `SkeletonData` / `ParameterBuffer`：把 CPU 侧 buffer 字节暴露给 descriptor 路径
- `CombinedTextureSampler`：把纹理像素和 shader binding 名暴露给采样器绑定路径

接口刻意保持得很薄，只表达 backend 真正需要的最小信息：

- `getType()`：决定后端要创建哪种 GPU 对象
- `getRawData()` + `getByteSize()`：提供 upload 源数据
- `getBindingName()`：让 descriptor 绑定按 shader 名字对齐
- dirty 标记：把“CPU 数据刚改过”显式传给资源同步阶段

因此它更像“GPU 资源适配接口”，而不是“渲染对象基类”。
*/
class IGpuResource {
public:
  IGpuResource() = default;
  virtual ~IGpuResource() = default;
  virtual ResourceType getType() const = 0;
  virtual const void *getRawData() const = 0;
  virtual ResourceByteSize32 getByteSize() const = 0;

  /// Shader-side binding name this resource fills (e.g.,
  /// StringID("CameraUBO")). Empty StringID means "unnamed" — such resources
  /// are routed via the material path (textures) or not routed at all
  /// (vertex/index buffers).
  virtual StringID getBindingName() const { return StringID{}; }

  /// Stable identity used by backend GPU-resource caches. This is assigned once
  /// per CPU resource object and is not derived from the current memory
  /// address, so a later allocation at the same address does not alias the
  /// old GPU cache entry.
  ResourceCacheIdentity getBackendCacheIdentity() const {
    return m_backendCacheIdentity;
  }

  // Legacy debug handle. Backend cache lookup should use
  // getBackendCacheIdentity() instead of the raw address.
  void *getResourceHandle() const { return (void *)this; }

  bool isDirty() const { return isDirty_; }
  void setDirty() { isDirty_ = true; }
  void clearDirty() { isDirty_ = false; }

private:
  static ResourceCacheIdentity nextBackendCacheIdentity() {
    static std::atomic<ResourceCacheIdentity> nextIdentity{1};
    return nextIdentity.fetch_add(1, std::memory_order_relaxed);
  }

  bool isDirty_ = false;
  const ResourceCacheIdentity m_backendCacheIdentity =
      nextBackendCacheIdentity();
};

using IGpuResourceSharedPtr = std::shared_ptr<IGpuResource>;

// Push-constant payload shared by the current render path. Keep this layout
// stable with the shader-side ABI unless the entire draw contract is migrated.
struct alignas(16) PerDrawLayoutBase {
  Mat4f model = Mat4f::identity();
};

/// Transitional alias for the current engine-wide draw push-constant ABI.
using PerDrawLayout = PerDrawLayoutBase;

} // namespace LX_core

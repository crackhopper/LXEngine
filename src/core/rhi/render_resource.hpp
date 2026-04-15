#pragma once
#include "core/math/mat.hpp"
#include "core/math/vec.hpp"
#include "core/platform/types.hpp"
#include "core/utils/string_table.hpp"
#include <memory>
#include <vector>

namespace LX_core {
// 资源所属的pass
enum class ResourcePassFlag : u32 {
  Forward = 0x00000001,
  Deferred = 0x00000002,
  Shadow = 0x00000004,
};

constexpr ResourcePassFlag operator|(ResourcePassFlag a, ResourcePassFlag b) {
  return static_cast<ResourcePassFlag>(static_cast<u32>(a) |
                                       static_cast<u32>(b));
}

constexpr ResourcePassFlag operator&(ResourcePassFlag a, ResourcePassFlag b) {
  return static_cast<ResourcePassFlag>(static_cast<u32>(a) &
                                       static_cast<u32>(b));
}

constexpr ResourcePassFlag All = ResourcePassFlag::Forward |
                                 ResourcePassFlag::Deferred |
                                 ResourcePassFlag::Shadow;

// 资源槽位类型，后端根据这个走不同的处理流程。
enum class ResourceType : u8 {
  None = 0,
  PushConstant,
  VertexBuffer,
  IndexBuffer,
  Shader,
  UniformBuffer,
  CombinedImageSampler,
  Special,
  Count
};

class IRenderResource {
public:
  virtual ~IRenderResource() = default;

  /// Default for geometry buffers and other resources that do not participate
  /// in pass scheduling; materials and UBOs override with their pass.
  virtual ResourcePassFlag getPassFlag() const {
    return ResourcePassFlag::Forward;
  }
  virtual ResourceType getType() const = 0;
  virtual const void *getRawData() const = 0;
  virtual u32 getByteSize() const = 0;

  /// Shader-side binding name this resource fills (e.g.,
  /// StringID("CameraUBO")). Empty StringID means "unnamed" — such resources
  /// are routed via the material path (textures) or not routed at all
  /// (vertex/index buffers).
  virtual StringID getBindingName() const { return StringID{}; }

  // 资源的唯一标识符，用于在渲染管线中查找资源
  // 直接使用地址作为句柄
  void *getResourceHandle() const { return (void *)this; }

  bool isDirty() const { return isDirty_; }
  void setDirty() { isDirty_ = true; }
  void clearDirty() { isDirty_ = false; }

private:
  bool isDirty_ = false;
};

using IRenderResourcePtr = std::shared_ptr<IRenderResource>;

// 确保与 GLSL 的 std140/std430 布局完全一致
struct alignas(16) PC_Base {
  Mat4f model = Mat4f::identity();
};

/// Push constants for forward draws (e.g. blinnphong_0-style vertex shader).
struct alignas(16) PC_Draw : public PC_Base {
  int32_t enableLighting = 1;
  int32_t enableSkinning = 0;
  int32_t padding[2];
};

} // namespace LX_core
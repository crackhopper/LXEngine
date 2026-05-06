#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/math/mat.hpp"
#include "core/math/vec.hpp"

#include <memory>
#include <optional>

namespace LX_core {

using VisibilityLayerMask = u32;
inline constexpr VisibilityLayerMask VisibilityMask_All = 0xffffffffu;

// CameraData is the GPU-facing UBO view of a Camera. The Camera object owns
// high-level parameters; this struct owns the packed bytes the backend uploads.
struct alignas(16) CameraData : public IGpuResource {
  struct Param {
    Mat4f view = Mat4f::identity();
    Mat4f proj = Mat4f::identity();
    Vec3f eyePos = Vec3f(0.0f, 0.0f, 0.0f);
    float pad; // 对齐
  };
  Param param{};

  virtual ResourceType getType() const override {
    return ResourceType::UniformBuffer;
  }
  virtual const void *getRawData() const override { return &param; }
  static constexpr u32 ResourceSize = sizeof(Param);
  virtual u32 getByteSize() const override {
    return ResourceSize;
  }

  StringID getBindingName() const override {
    static const StringID kName("CameraUBO");
    return kName;
  }

};

using CameraDataSharedPtr = std::shared_ptr<CameraData>;

// Camera 类型枚举
enum class CameraType { Perspective, Orthographic };

} // namespace LX_core

#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/math/mat.hpp"
#include "core/math/ray.hpp"
#include "core/math/vec.hpp"
#include "core/scene/visibility_mask.hpp"

#include <memory>
#include <optional>
#include <string>

namespace LX_core {

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

using CameraDataUniquePtr = std::unique_ptr<CameraData>;

// Camera 类型枚举
enum class CameraType { Perspective, Orthographic };

struct CameraPose final {
  Vec3f eye{0.0f, 0.0f, 0.0f};
  Vec3f forward{0.0f, 0.0f, -1.0f};
  Vec3f up{0.0f, 1.0f, 0.0f};
};

struct CameraProjection final {
  CameraType type = CameraType::Perspective;
  float fovYDegrees = 45.0f;
  float aspect = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  float left = -1.0f;
  float right = 1.0f;
  float bottom = -1.0f;
  float top = 1.0f;
};

struct CameraSnapshot final {
  std::string path;
  CameraPose pose;
  CameraProjection projection;
  VisibilityLayerMask cullingMask = Layer_All & ~Layer_EditorOverlay;
  bool active = true;
};

struct CameraRayFrame final {
  Vec3f eye{0.0f, 0.0f, 0.0f};
  Vec3f right{1.0f, 0.0f, 0.0f};
  Vec3f up{0.0f, 1.0f, 0.0f};
  Vec3f forward{0.0f, 0.0f, -1.0f};
};

[[nodiscard]] CameraPose makeCameraPose(Vec3f eye, Vec3f forward, Vec3f up);
[[nodiscard]] Mat4f makeCameraViewMatrix(const CameraPose &pose);
[[nodiscard]] Mat4f makeCameraProjectionMatrix(
    const CameraProjection &projection, GraphicsAPI api = GraphicsAPI::Vulkan);
[[nodiscard]] CameraRayFrame makeCameraRayFrame(
    const CameraPose &pose, const CameraProjection &projection);
[[nodiscard]] Ray makeCameraRay(const CameraPose &pose,
                                const CameraProjection &projection,
                                const Vec2f &screenPixel,
                                const Vec2f &viewportSize);

} // namespace LX_core

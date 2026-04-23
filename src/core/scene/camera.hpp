#pragma once
#include "core/rhi/gpu_resource.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/math/mat.hpp" // 假设你有 Mat4f 定义
#include "core/math/vec.hpp" // Vec3f
#include <cmath>
#include <memory>
#include <optional>

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
  static constexpr usize ResourceSize = sizeof(Param);
  virtual u32 getByteSize() const override { return ResourceSize; }

  StringID getBindingName() const override {
    static const StringID kName("CameraUBO");
    return kName;
  }

};

using CameraDataPtr = std::shared_ptr<CameraData>;

// Camera 类型枚举
enum class CameraType { Perspective, Orthographic };

// CPU 层 Camera 基类
class Camera {
public:
  Camera() { ubo = std::make_shared<CameraData>(); }
  virtual ~Camera() = default;

  CameraDataPtr getUBO() const { return ubo; }

  // ========================
  // 相机类型相关属性
  // ========================
  CameraType type = CameraType::Perspective;

  // 位置与方向
  Vec3f position = Vec3f(0.0f, 0.0f, 0.0f);
  Vec3f target = Vec3f(0.0f, 0.0f, -1.0f); // LookAt
  Vec3f up = Vec3f(0.0f, 1.0f, 0.0f);

  // 透视相机参数
  float fovY = 45.0f; // 垂直视角，单位：度
  float aspect = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;

  // 正交相机参数
  float left = -1.0f;
  float right = 1.0f;
  float bottom = -1.0f;
  float top = 1.0f;

  CameraDataPtr ubo;

  /// REQ-009: the RenderTarget this camera draws to. `nullopt` means
  /// "defaults to the swapchain" — `VulkanRenderer::initScene` is responsible
  /// for backfilling nullopt cameras with the real swapchain target before
  /// FrameGraph::buildFromScene runs. A nullopt camera does NOT match any
  /// concrete target (see matchesTarget), so tests that rely on filter hits
  /// must setTarget explicitly.
  const std::optional<RenderTarget> &getTarget() const { return m_target; }
  void setTarget(RenderTarget target) { m_target = std::move(target); }
  void clearTarget() { m_target.reset(); }

  /// True iff m_target has a value AND equals `target` field-by-field.
  /// nullopt cameras always return false — the backfill contract is on
  /// VulkanRenderer::initScene, not on this method.
  bool matchesTarget(const RenderTarget &target) const {
    return m_target.has_value() && *m_target == target;
  }

private:
  std::optional<RenderTarget> m_target;

public:
  // 更新矩阵（在渲染前调用）
  virtual void updateMatrices() {
    ubo->param.eyePos = position;
    ubo->param.view = Mat4f::lookAt(position, target, up);
    if (type == CameraType::Perspective) {
      const float fovYRad = fovY * (3.14159265358979323846f / 180.0f);
      ubo->param.proj =
          Mat4f::perspective(fovYRad, aspect, nearPlane, farPlane);
    } else {
      ubo->param.proj =
          Mat4f::orthographic(left, right, bottom, top, nearPlane, farPlane);
    }
    ubo->setDirty();
  }
};

using CameraPtr = std::shared_ptr<Camera>;

} // namespace LX_core

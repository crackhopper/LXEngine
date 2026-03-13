#pragma once
#include "../gpu/render_resource.hpp"
#include "../math/mat.hpp" // 假设你有 Mat4f 定义
#include "../math/vec.hpp" // Vec3f
#include "components/base.hpp"
#include <memory>
#include <optional>

namespace LX_core {

struct alignas(16) CameraUBO : public IRenderResource {
  struct Param {
    Mat4f view = Mat4f::identity();
    Mat4f proj = Mat4f::identity();
    Vec3f eyePos = Vec3f(0.0f, 0.0f, 0.0f);
    float pad; // 对齐
  };
  Param param{};

  CameraUBO(ResourcePassFlag passFlag) : m_passFlag(passFlag) {}

  virtual ResourcePassFlag getPassFlag() const override {
    return m_passFlag;
  }
  virtual ResourceType getType() const override {
    return ResourceType::DescriptorSet;
  }
  virtual const void *getRawData() const override {
    return &param;
  }
  virtual u32 getByteSize() const override {
    return sizeof(Param);
  }

  virtual PipelineSlotId getPipelineSlotId() const override {
    return PipelineSlotId::CameraUBO;
  }
private:
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};

using CameraUBOPtr = std::shared_ptr<CameraUBO>;

// Camera 类型枚举
enum class CameraType { Perspective, Orthographic };

// CPU 层 Camera 基类
class Camera : public IComponent {
public:
  Camera(ResourcePassFlag passFlag) {
    ubo = std::make_shared<CameraUBO>(passFlag);
  }
  virtual ~Camera() = default;

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

  CameraUBOPtr ubo;

  // 更新矩阵（在渲染前调用）
  virtual void updateMatrices() {
    ubo->param.eyePos = position;
    ubo->param.view = Mat4f::lookAt(position, target, up);
    if (type == CameraType::Perspective) {
      ubo->param.proj = Mat4f::perspective(fovY, aspect, nearPlane, farPlane);
    } else {
      ubo->param.proj =
          Mat4f::orthographic(left, right, bottom, top, nearPlane, farPlane);
    }
    ubo->setDirty();
  }

  // 获取当前相机UBO数据
  virtual std::vector<IRenderResourcePtr> getRenderResources() override {
    return {
      std::dynamic_pointer_cast<IRenderResource>(ubo)
    };
  }
};

using CameraPtr = std::shared_ptr<Camera>;

} // namespace LX_core
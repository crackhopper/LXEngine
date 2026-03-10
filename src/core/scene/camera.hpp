#pragma once
#include "../math/mat.hpp" // 假设你有 Mat4f 定义
#include "../math/vec.hpp" // Vec3f
#include <memory>
#include <optional>

namespace LX_core {

// Camera 类型枚举
enum class CameraType { Perspective, Orthographic };

// CPU 层 Camera 基类
class Camera {
public:
  Camera() = default;
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

  // ========================
  // 矩阵缓存
  // ========================
  Mat4f viewMatrix;
  Mat4f projMatrix;

  // 更新矩阵（在渲染前调用）
  virtual void updateMatrices() {
    viewMatrix = Mat4f::lookAt(position, target, up);
    if (type == CameraType::Perspective) {
      projMatrix = Mat4f::perspective(fovY, aspect, nearPlane, farPlane);
    } else {
      projMatrix =
          Mat4f::orthographic(left, right, bottom, top, nearPlane, farPlane);
    }
  }

  // ========================
  // DescriptorSet 数据接口（CPU层数据，用于GPU层填充UBO）
  // ========================
  struct CameraUBO {
    Mat4f view;
    Mat4f proj;
    Mat4f viewProj; // 方便shader使用
    Vec3f eyePos;
    float pad; // 对齐
  };

  // 获取当前相机UBO数据
  CameraUBO getUBO() const {
    CameraUBO ubo{};
    ubo.view = viewMatrix;
    ubo.proj = projMatrix;
    ubo.viewProj = projMatrix * viewMatrix;
    ubo.eyePos = position;
    return ubo;
  }
};

using CameraPtr = std::shared_ptr<Camera>;

} // namespace LX_core
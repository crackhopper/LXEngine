#pragma once
#include "core/math/mat.hpp"
#include "core/scene/scene.hpp"
#include "core/platform/window.hpp"

#include <memory>
#include <vector>

namespace LX_core::gpu {

class Renderer {
public:
  virtual ~Renderer() = default;

  // 初始化 GPU 设备 / context
  virtual void initialize(WindowPtr window, const char *appName) = 0;

  // 清理 GPU 资源
  virtual void shutdown() = 0;

  // 初始化，根据场景创建后端资源。
  virtual void initScene(ScenePtr scene) = 0;

  // --- 每帧的动作
  // 逻辑计算（力学模拟，通常比较简单，刚体力学为主） + 上传数据
  virtual void uploadData() = 0;
  // 绘制渲染对象：录制命令+提交
  virtual void draw() = 0;
  
};

using RendererPtr = std::shared_ptr<Renderer>;

} // namespace LX_core::gpu
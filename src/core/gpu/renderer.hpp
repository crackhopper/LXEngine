#pragma once
#include "../math/mat.hpp"
#include "../scene/scene.hpp"

#include <memory>
#include <vector>

namespace LX_core::gpu {

class Renderer {
public:
  virtual ~Renderer() = default;

  // 初始化 GPU 设备 / context
  virtual void initialize() = 0;

  // 清理 GPU 资源
  virtual void shutdown() = 0;

  // 初始化，根据场景创建后端资源。
  virtual void initScene(ScenePtr scene) = 0;

  // 上传数据
  virtual void uploadData() = 0;

  // 绘制渲染对象：录制命令+提交
  virtual void draw() = 0;
  
};

using RendererPtr = std::shared_ptr<Renderer>;

} // namespace LX_core::gpu
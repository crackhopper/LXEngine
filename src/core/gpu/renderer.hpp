#pragma once
#include "../math/mat.hpp"
#include "../scene/object.hpp"

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

  // 添加渲染对象
  virtual void addRenderObject(RenderableMeshPtr<VertexType> renderableMesh) = 0;

  // 同步渲染数据
  virtual void syncRenderObjects() = 0;

  // 绘制渲染对象：录制命令+提交
  virtual void draw() = 0;
  
};

using RendererPtr = std::shared_ptr<Renderer>;

} // namespace LX_core::gpu
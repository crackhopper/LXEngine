#pragma once
#include "../resources/buffer.hpp"
#include "../resources/material.hpp"
#include "../resources/mesh.hpp"
#include "../resources/texture.hpp"
#include "../math/mat.hpp"

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

  // 上传 Mesh 到 GPU（VertexBuffer + IndexBuffer）
  virtual void uploadMesh(LX_core::MeshPtr mesh) = 0;

  // 上传 Texture 到 GPU
  virtual void uploadTexture(LX_core::TexturePtr texture) = 0;

  // 绘制 Mesh + Material（材质控制 shader / 贴图绑定）
  virtual void drawMesh(LX_core::MeshPtr mesh,
                         LX_core::MaterialPtr material) = 0;

  // 可选：设置 View/Projection 矩阵
  virtual void setCamera(const LX_core::Mat4f &view,
                         const LX_core::Mat4f &proj) = 0;

  // 可选：提交渲染命令（如果 backend 需要）
  virtual void flush() = 0;
};

using RendererPtr = std::shared_ptr<Renderer>;

} // namespace LX_core::gpu
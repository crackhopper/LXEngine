// game_app.cpp
#include "core/gpu/renderer.hpp"
#include "core/resources/material.hpp"
#include "core/resources/mesh.hpp"
#include "core/resources/texture.hpp"
#include "core/resources/vertex.hpp"

// backend Vulkan 实现
#include "graphics_backend/vulkan/vk_renderer.hpp"

// 窗口系统
#include "infra/window/window.hpp"

#include <memory>

using namespace LX_core;
using namespace LX_core::gpu;

int main() {
  LX_infra::WindowImpl::Initialize();
  LX_core::WindowPtr window =
      std::make_shared<LX_infra::WindowImpl>("Test Renderer", 800, 600);
  // 创建 Renderer（Vulkan 后端）
  RendererPtr renderer =
      std::make_shared<LX_core::graphic_backend::VulkanRenderer>(window);
  renderer->initialize();

  using VType = VertexPosColor;
  using VB = VertexBuffer<VType>;
  // 创建 Mesh（CPU 内存数据）
  auto vertexBuffer = std::make_unique<VB>(std::vector<VType>{
      VertexPosColor({0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}),
      VertexPosColor({0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}),
      VertexPosColor({-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}),
  });
  auto indexBuffer = std::make_unique<IndexBuffer>(std::vector<u32>{0, 1, 2});
  auto subMesh = std::make_unique<SubMesh>(std::move(vertexBuffer),
                                           std::move(indexBuffer));

  MeshPtr mesh = std::make_shared<Mesh>();
  mesh->addSubMesh(std::move(subMesh));

  // 创建 Material
  MaterialPtr material = std::make_shared<Material>();
  // 暂时贴图也不支持，先注释掉
  // auto albedo = std::make_shared<Texture>();
  // material.albedoMap = albedo;

  // 上传资源到 GPU
  renderer->uploadMesh(mesh);
  if (material->albedoMap.has_value())
    renderer->uploadTexture(material->albedoMap.value());

  // 渲染循环
  bool running = true;
  window->onClose([&running]() { running = false; });
  while (running) {
    // 设置摄像机矩阵
    Mat4f view = Mat4f::lookAt({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f},
                               {0.0f, 1.0f, 0.0f});
    Mat4f proj = Mat4f::perspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);
    renderer->setCamera(view, proj);

    // 绘制
    renderer->drawMesh(mesh, material);

    // 提交渲染命令
    renderer->flush();
  }

  // 清理
  renderer->shutdown();

  return 0;
}
// game_app.cpp
#include "core/gpu/renderer.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/texture.hpp"
#include "core/resources/vertex_buffer.hpp"

// backend Vulkan 实现
#include "backend/vulkan/vk_renderer.hpp"

// 窗口系统
#include "infra/window/window.hpp"

#include "core/scene/components/material.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

using namespace LX_core;
using namespace LX_core::gpu;

namespace {
bool testDebugEnabled() {
  static const bool enabled = [] {
    const char *value = std::getenv("LX_RENDER_DEBUG");
    return value != nullptr && std::strcmp(value, "0") != 0;
  }();
  return enabled;
}
} // namespace

int main() {
  LX_infra::Window::Initialize();
  LX_core::WindowPtr window =
      std::make_shared<LX_infra::Window>("Test Renderer", 800, 600);

  // 创建 Renderer（Vulkan 后端）
  RendererPtr renderer =
      std::make_shared<LX_core::backend::VulkanRenderer>(
          LX_core::backend::VulkanRenderer::Token{});
  renderer->initialize(window, "TestRenderTriangle");

  auto vertexBufferPtr = VertexBuffer<VertexPosNormalUvBone>::create({
      VertexPosNormalUvBone({-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                            {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      VertexPosNormalUvBone({1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                            {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      VertexPosNormalUvBone({1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                            {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
  });

  // With the current Vulkan viewport/cull setup, this winding must stay
  // consistent with the integration test or the triangle is back-face culled.
  auto indexBufferPtr = IndexBuffer::create({0, 1, 2});
  auto meshPtr =
      Mesh<VertexPosNormalUvBone>::create(vertexBufferPtr, indexBufferPtr);

  // Build a renderable mesh with a material (Scene expects IRenderable).
  auto material = MaterialBlinnPhong::create();
  material->ubo->params.enableNormalMap =
      0; // avoid needing correct tangents for N mapping
  material->ubo->setDirty();

  auto skeletonPtr = Skeleton::create({});

  auto renderable = std::make_shared<RenderableSubMesh<VertexPosNormalUvBone>>(
      meshPtr, material, skeletonPtr);

  auto scene = Scene::create(renderable);
  renderer->initScene(scene);

  // Provide a default directional light; shader expects LightUBO values.
  if (scene->directionalLight && scene->directionalLight->ubo) {
    scene->directionalLight->ubo->param.dir = Vec4f{0.0f, -1.0f, 0.0f, 0.0f};
    scene->directionalLight->ubo->param.color = Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
    scene->directionalLight->ubo->setDirty();
  }

  // 渲染循环
  bool running = true;
  uint64_t frameCounter = 0;
  window->onClose([&running]() { running = false; });
  if (testDebugEnabled()) {
    std::cerr << "[TriangleTest] window=" << window->getWidth() << "x"
              << window->getHeight() << ", vertexBytes="
              << vertexBufferPtr->getByteSize() << ", indexBytes="
              << indexBufferPtr->getByteSize() << ", indices={0,1,2}"
              << std::endl;
  }
  while (running) {
    // 处理窗口事件，防止窗口卡住
    if (window->shouldClose()) {
      running = false;
      break;
    }

    // 设置摄像机矩阵
    scene->camera->position = {0.0f, 0.0f, 3.0f};
    scene->camera->target = {0.0f, 0.0f, 0.0f};
    scene->camera->up = Vec3f(0.0f, 1.0f, 0.0f);
    scene->camera->aspect = 800.0f / 600.0f;

    scene->camera->updateMatrices();
    if (testDebugEnabled() && frameCounter < 3) {
      std::cerr << "[TriangleTest] frame=" << frameCounter << ", cameraPos=("
                << scene->camera->position.x << "," << scene->camera->position.y
                << "," << scene->camera->position.z << "), target=("
                << scene->camera->target.x << "," << scene->camera->target.y
                << "," << scene->camera->target.z << "), aspect="
                << scene->camera->aspect << std::endl;
      if (frameCounter == 0) {
        const auto &view = scene->camera->ubo->param.view;
        const auto &proj = scene->camera->ubo->param.proj;
        const std::array<Vec4f, 3> debugPositions = {
            Vec4f{-1.0f, 1.0f, 0.0f, 1.0f},
            Vec4f{1.0f, 1.0f, 0.0f, 1.0f},
            Vec4f{1.0f, -1.0f, 0.0f, 1.0f},
        };
        for (size_t i = 0; i < debugPositions.size(); ++i) {
          const Vec4f viewPos = view * debugPositions[i];
          const Vec4f clipPos = proj * viewPos;
          std::cerr << "[TriangleTest] vertex" << i << " view=(" << viewPos.x
                    << "," << viewPos.y << "," << viewPos.z << ","
                    << viewPos.w << "), clip=(" << clipPos.x << ","
                    << clipPos.y << "," << clipPos.z << "," << clipPos.w
                    << ")";
          if (clipPos.w != 0.0f) {
            std::cerr << ", ndc=(" << clipPos.x / clipPos.w << ","
                      << clipPos.y / clipPos.w << ","
                      << clipPos.z / clipPos.w << ")";
          }
          std::cerr << std::endl;
        }
      }
    }
    renderer->uploadData();
    renderer->draw();
    frameCounter++;
    if (testDebugEnabled() && frameCounter % 120 == 0) {
      std::cerr << "[TriangleTest] heartbeat frame=" << frameCounter
                << std::endl;
    }
  }

  // 清理
  renderer->shutdown();

  return 0;
}
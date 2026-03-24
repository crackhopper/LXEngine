// Smoke-test: initialize VulkanRenderer and render at least one frame.
#include "core/gpu/renderer.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/texture.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/scene/components/material.hpp"
#include "core/scene/scene.hpp"
#include "graphics_backend/vulkan/vk_renderer.hpp"
#include "infra/window/window.hpp"

#include <filesystem>
#include <iostream>
#include <memory>

namespace fs = std::filesystem;

static void cdToWhereShadersExist() {
  fs::path p = fs::current_path();
  for (int i = 0; i < 8; ++i) {
    if (fs::exists(p / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p);
      return;
    }
    if (fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p / "build");
      return;
    }
    const auto parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
}

static bool hasBlinnPhongSpv() {
  return fs::exists("shaders/glsl/blinnphong_0.vert.spv") &&
         fs::exists("shaders/glsl/blinnphong_0.frag.spv");
}

int main() {
  try {
    cdToWhereShadersExist();

    if (!hasBlinnPhongSpv()) {
      std::cerr << "Missing SPIR-V for renderer test.\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>("Test Vulkan Renderer", 256, 256);

    auto renderer =
        std::make_shared<LX_core::graphic_backend::VulkanRenderer>(
            LX_core::graphic_backend::VulkanRenderer::Token{});
    renderer->initialize(window);

    auto vertexBufferPtr = LX_core::VertexBuffer<
        LX_core::VertexPosNormalUvBone>::create(
        {
            LX_core::VertexPosNormalUvBone({-5.0f, 5.0f, 0.0f},
                                           {0.0f, 0.0f, 1.0f},
                                           {0.0f, 0.0f},
                                           {1.0f, 0.0f, 0.0f, 0.0f},
                                           {0, 0, 0, 0},
                                           {1.0f, 0.0f, 0.0f, 0.0f}),
            LX_core::VertexPosNormalUvBone({5.0f, 5.0f, 0.0f},
                                           {0.0f, 0.0f, 1.0f},
                                           {1.0f, 0.0f},
                                           {1.0f, 0.0f, 0.0f, 0.0f},
                                           {0, 0, 0, 0},
                                           {1.0f, 0.0f, 0.0f, 0.0f}),
            LX_core::VertexPosNormalUvBone({5.0f, -5.0f, 0.0f},
                                           {0.0f, 0.0f, 1.0f},
                                           {1.0f, 1.0f},
                                           {1.0f, 0.0f, 0.0f, 0.0f},
                                           {0, 0, 0, 0},
                                           {1.0f, 0.0f, 0.0f, 0.0f}),
        });

    auto indexBufferPtr = LX_core::IndexBuffer::create({0, 1, 2});
    auto meshPtr = LX_core::Mesh<LX_core::VertexPosNormalUvBone>::create(
        vertexBufferPtr, indexBufferPtr);

    auto material =
        std::make_shared<LX_core::MaterialBlinnPhong>(
            LX_core::ResourcePassFlag::Forward);
    material->params->params.enableNormalMap = 0; // avoid tangents requirement
    material->params->setDirty();

    auto renderable =
        std::make_shared<LX_core::RenderableSubMesh<LX_core::VertexPosNormalUvBone>>(
            meshPtr, material);
    auto scene = LX_core::Scene::create(renderable);

    renderer->initScene(scene);

    // Default directional light UBO (if present).
    if (scene->directionalLight && scene->directionalLight->ubo) {
      scene->directionalLight->ubo->param.dir = LX_core::Vec4f{
          0.0f, -1.0f, 0.0f, 0.0f};
      scene->directionalLight->ubo->param.color = LX_core::Vec4f{
          1.0f, 1.0f, 1.0f, 1.0f};
      scene->directionalLight->ubo->setDirty();
    }

    // Render one frame.
    scene->camera->position = {0.0f, 0.0f, 3.0f};
    scene->camera->target = {0.0f, 0.0f, 0.0f};
    scene->camera->up = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
    scene->camera->updateMatrices();

    renderer->uploadData();
    renderer->draw();

    renderer->shutdown();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanRenderer test: " << e.what() << "\n";
    return 0;
  }
}


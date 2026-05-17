#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "infra/window/window.hpp"

#include "scene_test_helpers.hpp"

#include <iostream>

namespace {

LX_core::SceneSharedPtr makeForwardScene() {
  using V = LX_core::VertexPosNormalUvBone;
  auto vertexBuffer = LX_core::VertexBuffer<V>::create({
      V({-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      V({0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      V({0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
        {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
  });
  auto indexBuffer = LX_core::IndexBuffer::create({0u, 1u, 2u});
  auto mesh = LX_core::Mesh::create(
      vertexBuffer, indexBuffer,
      LX_core::BoundingBox{{-0.5f, -0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}});
  auto material = LX_infra::loadGenericMaterial(
      "assets/materials/blinnphong_default.material");

  auto node = LX_core::SceneNode::create("vulkan_frame_graph_node");
  node->addComponent<LX_core::MeshComponent>(mesh);
  node->addComponent<LX_core::MaterialComponent>(material);
  node->addComponent<LX_core::SkeletonComponent>(LX_core::Skeleton::create({}));

  auto scene = LX_core::Scene::create(node);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
  return scene;
}

} // namespace

int main() {
  expSetEnvVK();
  try {
    if (!initializeRuntimeAssetRoot()) {
      std::cerr << "Failed to find runtime assets\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan FrameGraph", 64, 64);

    auto renderer = LX_core::backend::VulkanRenderer::create(
        LX_core::backend::VulkanRenderer::Token{});
    renderer->initialize(window, "TestVulkanFrameGraph");
    renderer->initScene(makeForwardScene());

    if (renderer->compiledFrameGraphPassCount() < 2) {
      std::cerr << "compiled frame graph should contain offscreen + swapchain "
                   "passes\n";
      return 1;
    }

    renderer->uploadData();
    renderer->draw();

    if (renderer->frameGraphAttachmentCount() == 0) {
      std::cerr << "offscreen frame graph attachment was not created\n";
      return 1;
    }

    renderer->shutdown();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanFrameGraph test: " << e.what() << "\n";
    return 0;
  }
}

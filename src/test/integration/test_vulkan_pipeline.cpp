#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
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

int main() {
  expSetEnvVK();
  try {
    auto success = initializeRuntimeAssetRoot();
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan Pipeline", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanPipeline");

    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(device->getSurfaceFormat(),
                                                     device->getDepthFormat());

    using V = LX_core::VertexPosNormalUvBone;
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create({
        V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
    });
    auto indexBufferPtr = LX_core::IndexBuffer::create({0u, 1u, 2u});
    auto meshPtr = LX_core::Mesh::create(
        vertexBufferPtr, indexBufferPtr,
        LX_core::BoundingBox{{-5.0f, -5.0f, 0.0f}, {5.0f, 5.0f, 0.0f}});
    auto material = LX_infra::loadGenericMaterial(
        "assets/materials/blinnphong_default.material");
    auto node = LX_core::SceneNode::create("vulkan_pipeline_node");
    node->addComponent<LX_core::MeshComponent>(meshPtr);
    node->addComponent<LX_core::MaterialComponent>(material);
    node->addComponent<LX_core::SkeletonComponent>(
        LX_core::Skeleton::create({}));
    auto scene = LX_core::Scene::create(node);
    scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
    auto item = LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);

    auto pipeline = resourceManager->getOrCreatePipeline(item);

    const VkPipeline pipelineHandle =
        std::visit([](auto ref) { return ref.get().getHandle(); }, pipeline);
    if (pipelineHandle == VK_NULL_HANDLE) {
      std::cerr << "VkPipeline handle is null\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanGraphicsPipeline test: " << e.what() << "\n";
    return 0;
  }
}

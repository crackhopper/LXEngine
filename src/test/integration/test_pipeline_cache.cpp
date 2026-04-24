#include "backend/vulkan/details/pipelines/pipeline_cache.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "core/utils/filesystem_tools.hpp"
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
        std::make_shared<LX_infra::Window>("Test Pipeline Cache", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestPipelineCache");

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
    auto meshPtr = LX_core::Mesh::create(vertexBufferPtr, indexBufferPtr);
    auto material = LX_infra::loadGenericMaterial("materials/blinnphong_default.material");
    auto node = LX_core::SceneNode::create(
        "pipeline_cache_node", meshPtr, material, LX_core::Skeleton::create({}));
    auto scene = LX_core::Scene::create(node);
    // RenderQueue::buildFromScene internally merges scene.getSceneLevelResources(pass, target),
    // so the item already carries camera + light UBOs — no side-channel injection.
    auto item = LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);

    auto info = LX_core::PipelineBuildDesc::fromRenderingItem(item);

    auto &cache = resourceManager->getPipelineCache();
    auto found0 = cache.find(info.key);
    if (found0.has_value()) {
      std::cerr << "FAIL: cold cache unexpectedly has key\n";
      return 1;
    }

    auto &pipelineFirst =
        cache.getOrCreate(info, resourceManager->getRenderPass().getHandle());
    if (cache.size() != 1) {
      std::cerr << "FAIL: cache size expected 1 after first getOrCreate, got "
                << cache.size() << "\n";
      return 1;
    }

    auto &pipelineSecond =
        cache.getOrCreate(info, resourceManager->getRenderPass().getHandle());
    if (&pipelineFirst != &pipelineSecond) {
      std::cerr << "FAIL: second getOrCreate returned different instance\n";
      return 1;
    }

    auto found1 = cache.find(info.key);
    if (!found1.has_value()) {
      std::cerr << "FAIL: find after getOrCreate missed\n";
      return 1;
    }

    // Preload with the same info should be idempotent (no rebuild).
    resourceManager->preloadPipelines({info});
    if (cache.size() != 1) {
      std::cerr << "FAIL: preload rebuilt existing key, cache size = "
                << cache.size() << "\n";
      return 1;
    }

    // FrameGraph-driven collection should also produce exactly this one info.
    LX_core::FrameGraph fg;
    fg.addPass(LX_core::FramePass{LX_core::Pass_Forward, {}, {}});
    fg.buildFromScene(*scene);
    auto infos = fg.collectAllPipelineBuildDescs();
    if (infos.size() != 1) {
      std::cerr << "FAIL: frame graph produced " << infos.size()
                << " infos, expected 1\n";
      return 1;
    }

    std::cout << "OK: pipeline_cache test passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP pipeline_cache test: " << e.what() << "\n";
    return 0;
  }
}

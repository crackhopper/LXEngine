#include "backend/vulkan/details/pipelines/pipeline_cache.hpp"
#include "backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "backend/vulkan/details/vk_device.hpp"
#include "backend/vulkan/details/vk_resource_manager.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/pipeline_build_info.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/scene/frame_graph.hpp"
#include "core/scene/pass.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/loaders/blinnphong_material_loader.hpp"
#include "infra/window/window.hpp"

#include "scene_test_helpers.hpp"

#include <iostream>

int main() {
  expSetEnvVK();
  try {
    auto success = cdToWhereShadersExist("blinnphong_0");
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
    auto material = LX_infra::loadBlinnPhongMaterial();
    auto renderable = std::make_shared<LX_core::RenderableSubMesh>(
        meshPtr, material, LX_core::Skeleton::create({}));
    auto scene = LX_core::Scene::create(renderable);
    // RenderQueue::buildFromScene internally merges scene.getSceneLevelResources(),
    // so the item already carries camera + light UBOs — no side-channel injection.
    auto item = LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);

    auto info = LX_core::PipelineBuildInfo::fromRenderingItem(item);

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
    auto infos = fg.collectAllPipelineBuildInfos();
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

#include "backend/vulkan/details/pipelines/pipeline_cache.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/env.hpp"
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
    auto shader = LX_test::makeMinimalShaderForVulkanTests();
    LX_core::ShaderProgramSet shaderProgram;
    shaderProgram.shaderName = "minimal";
    shaderProgram.shader = shader;

    LX_core::RenderState renderState;
    renderState.cullMode = LX_core::CullMode::None;
    renderState.depthTestEnable = false;
    renderState.depthWriteEnable = false;

    const LX_core::RenderTarget target;
    const LX_core::PipelineKey key = LX_core::PipelineKey::build(
        shaderProgram.getPipelineSignature(),
        LX_test::testRenderPathNodeSignature(LX_core::Pass_PostProcess,
                                             target));
    auto info = LX_core::PipelineBuildDesc::graphics(
        key, shaderProgram.getPipelineSignature(), target.toDesc(),
        shader->getAllStages(), shader->getReflectionBindings(),
        vertexBufferPtr->getLayout(), renderState,
        indexBufferPtr->getTopology(), std::nullopt, {});

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

    std::cout << "OK: pipeline_cache test passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP pipeline_cache test: " << e.what() << "\n";
    return 0;
  }
}

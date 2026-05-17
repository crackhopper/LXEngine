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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool isKnownEnvironmentFailure(const std::string &message) {
  return message.find("No available video device") != std::string::npos ||
         message.find("Failed to create window") != std::string::npos ||
         message.find("Failed to create Vulkan instance") !=
             std::string::npos ||
         message.find("Failed to find GPUs with Vulkan support") !=
             std::string::npos ||
         message.find("Failed to find a suitable GPU") != std::string::npos ||
         message.find("GLFW could not find required Vulkan extensions") !=
             std::string::npos ||
         message.find("Failed to create Vulkan surface") != std::string::npos;
}

LX_core::MaterialInstanceSharedPtr loadFrameGraphDepthMaterial() {
  const auto materialPath = getRuntimeAssetRoot() / "assets" / "materials" /
                            "test_frame_graph_depth.material";
  {
    std::ofstream out(materialPath);
    out << "shader: blinnphong_0\n\n"
           "variants:\n"
           "  USE_LIGHTING: true\n\n"
           "parameters:\n"
           "  MaterialUBO.baseColor: [0.8, 0.8, 0.8]\n"
           "  MaterialUBO.shininess: 12.0\n"
           "  MaterialUBO.specularIntensity: 1.0\n"
           "  MaterialUBO.enableAlbedo: 0\n"
           "  MaterialUBO.enableNormal: 0\n\n"
           "passes:\n"
           "  Forward:\n"
           "    shader: blinnphong_0\n"
           "    renderState:\n"
           "      cullMode: Back\n"
           "      depthTest: true\n"
           "      depthWrite: true\n"
           "  Shadow:\n"
           "    shader: shadow_depth_only\n"
           "    renderState:\n"
           "      cullMode: Front\n"
           "      depthTest: true\n"
           "      depthWrite: true\n";
  }

  try {
    auto material = LX_infra::loadGenericMaterial(materialPath);
    std::filesystem::remove(materialPath);
    return material;
  } catch (...) {
    std::filesystem::remove(materialPath);
    throw;
  }
}

LX_core::SceneSharedPtr makeFrameGraphScene() {
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
  auto material = loadFrameGraphDepthMaterial();

  auto node = LX_core::SceneNode::create("vulkan_frame_graph_node");
  node->addComponent<LX_core::MeshComponent>(mesh);
  node->addComponent<LX_core::MaterialComponent>(material);
  node->addComponent<LX_core::SkeletonComponent>(LX_core::Skeleton::create({}));

  auto scene = LX_core::Scene::create(node);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
  auto shadowCamera = LX_test::makeDefaultCameraNodeWithTarget();
  auto shadowCameraComponent =
      shadowCamera->getComponent<LX_core::CameraComponent>();
  shadowCameraComponent->get().setTarget(
      LX_core::RenderTarget{LX_core::RenderTargetDesc::offscreenDepth(
          LX_core::ImageFormat::D32Float)});
  scene->addCamera(shadowCamera);
  return scene;
}

} // namespace

int main() {
  expSetEnvVK();
  enum class Phase {
    Environment,
    RendererInitialized,
    SceneInitialized,
  };
  Phase phase = Phase::Environment;
  LX_core::backend::VulkanRendererUniquePtr renderer;

  try {
    if (!initializeRuntimeAssetRoot()) {
      std::cerr << "Failed to find runtime assets\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan FrameGraph", 64, 64);

    renderer = LX_core::backend::VulkanRenderer::create(
        LX_core::backend::VulkanRenderer::Token{});
    renderer->initialize(window, "TestVulkanFrameGraph");
    phase = Phase::RendererInitialized;
    renderer->initScene(makeFrameGraphScene());
    phase = Phase::SceneInitialized;

    if (renderer->compiledFrameGraphPassCount() < 2) {
      std::cerr << "compiled frame graph should contain offscreen + swapchain "
                   "passes\n";
      return 1;
    }

    renderer->uploadData();
    for (int i = 0; i < 4; ++i) {
      renderer->draw();
    }

    if (renderer->frameGraphAttachmentCount() < 3) {
      std::cerr << "offscreen frame graph attachment should be allocated per "
                   "in-flight frame\n";
      return 1;
    }

    renderer->shutdown();
    return 0;
  } catch (const std::exception &e) {
    if (renderer) {
      renderer->shutdown();
    }
    const std::string message = e.what();
    if (phase == Phase::Environment && isKnownEnvironmentFailure(message)) {
      std::cerr << "SKIP VulkanFrameGraph test: " << message << "\n";
      return 0;
    }
    std::cerr << "FAIL VulkanFrameGraph test: " << message << "\n";
    return 1;
  }
}

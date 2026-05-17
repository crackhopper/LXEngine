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
#include <cmath>
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

  auto lightNode = LX_core::SceneNode::create("frame_graph_light");
  lightNode->setName("frame_graph_light");
  scene->addRenderable(lightNode);
  scene->attachLight(lightNode, std::make_shared<LX_core::DirectionalLight>());
  return scene;
}

bool matricesNearlyEqual(const LX_core::Mat4f &a, const LX_core::Mat4f &b) {
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (std::abs(a(row, col) - b(row, col)) > 1e-4f) {
        return false;
      }
    }
  }
  return true;
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
    auto scene = makeFrameGraphScene();
    renderer->initScene(scene);
    phase = Phase::SceneInitialized;

    if (renderer->compiledFrameGraphPassCount() < 5) {
      std::cerr << "compiled frame graph should contain four shadow cascades "
                   "+ swapchain passes\n";
      return 1;
    }

    renderer->uploadData();
    const auto light =
        std::dynamic_pointer_cast<LX_core::DirectionalLight>(
            scene->getLights().front());
    if (!light) {
      std::cerr << "frame graph scene should have a directional light\n";
      return 1;
    }
    const auto cascadeBeforeCameraMove =
        light->getDirectionalUBO()->param.cascadeViewProj[0];
    auto activeCamera =
        scene->getCameras().front()->getComponent<LX_core::CameraComponent>();
    activeCamera->get().lookAt({8.0f, 5.0f, 8.0f}, {0.0f, 0.0f, 0.0f},
                               {0.0f, 1.0f, 0.0f});
    renderer->uploadData();
    const auto cascadeAfterCameraMove =
        light->getDirectionalUBO()->param.cascadeViewProj[0];
    if (matricesNearlyEqual(cascadeBeforeCameraMove, cascadeAfterCameraMove)) {
      std::cerr << "directional shadow cascade should update when active "
                   "camera moves before upload\n";
      return 1;
    }

    for (int i = 0; i < 4; ++i) {
      renderer->draw();
    }

    if (renderer->frameGraphAttachmentCount() < 12) {
      std::cerr << "CSM depth attachments should be allocated per cascade and "
                   "per in-flight frame\n";
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

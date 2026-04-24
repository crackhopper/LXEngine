// REQ-019: default integration demo.
//
// Wires:
//   cdToWhereAssetsExist -> Window -> VulkanRenderer -> Scene (helmet + ground
//   + default camera/light) -> EngineLoop -> setDrawUiCallback -> run().
//
// All per-frame logic lives in the update hook registered with EngineLoop.

#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include "camera_rig.hpp"
#include "scene_builder.hpp"
#include "ui_overlay.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <imgui.h>
#include <iostream>
#include <memory>

using LX_core::backend::VulkanRenderer;
using LX_core::gpu::EngineLoop;

namespace demo = LX_demo::scene_viewer;

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

} // namespace

int main() {
  expSetEnvVK();
  // REQ-010: centralise the working directory on the assets root so relative
  // paths in material files, glTF buffers, and textures all resolve.
  if (!cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")) {
    std::cerr << "[scene_viewer] failed to locate assets via "
                 "cdToWhereAssetsExist\n";
    return 1;
  }

  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>(
        "demo_scene_viewer", kWindowWidth, kWindowHeight);

    // Keep the concrete VulkanRenderer alongside the base RendererSharedPtr: the
    // base handle feeds EngineLoop, the concrete handle reaches
    // setDrawUiCallback (per REQ-017 that callback is not on the base class).
    auto vulkanRenderer = std::make_shared<VulkanRenderer>(VulkanRenderer::Token{});
    LX_core::gpu::RendererSharedPtr renderer = vulkanRenderer;
    renderer->initialize(window, "demo_scene_viewer");

    // Build the scene. Helmet is the initial renderable passed to Scene;
    // ground is added afterwards via addRenderable().
    const std::filesystem::path gltfPath =
        "assets/models/damaged_helmet/DamagedHelmet.gltf";
    auto helmet = demo::buildHelmetNode(gltfPath);
    auto ground = demo::buildGroundNode();

    auto scene = LX_core::Scene::create("scene_viewer", helmet);
    scene->addRenderable(ground);

    auto camera = scene->getCameras().front();
    camera->position = LX_core::Vec3f{2.5f, 1.5f, 3.0f};
    camera->target = LX_core::Vec3f{0.0f, 0.0f, 0.0f};
    camera->up = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
    camera->aspect = static_cast<float>(kWindowWidth)
                     / static_cast<float>(kWindowHeight);
    camera->updateMatrices();

    auto dirLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
        scene->getLights().front());
    if (dirLight && dirLight->ubo) {
      dirLight->ubo->param.dir = LX_core::Vec4f{-0.3f, -1.0f, -0.5f, 0.0f};
      dirLight->ubo->param.color = LX_core::Vec4f{1.0f, 0.98f, 0.9f, 1.0f};
      dirLight->ubo->setDirty();
    }

    demo::CameraRig rig;
    rig.attach(camera.get());

    demo::UiOverlay ui;
    ui.attach(/*clock*/ nullptr, camera.get(), dirLight.get(), &rig);

    // Hand the UI callback to the concrete VulkanRenderer. Per REQ-017 the
    // callback is intentionally not on the gpu::Renderer base.
    vulkanRenderer->setDrawUiCallback([&] { ui.drawFrame(); });

    EngineLoop loop;
    loop.initialize(window, renderer);
    loop.startScene(scene);

    // Late-bind the clock now that EngineLoop owns one.
    ui.attach(&loop.getClock(), camera.get(), dirLight.get(), &rig);

    auto input = window->getInputState();

    loop.setUpdateHook([&](LX_core::Scene&, const LX_core::Clock& clock) {
      const bool imguiReady = ImGui::GetCurrentContext() != nullptr;
      const ImGuiIO* io = imguiReady ? &ImGui::GetIO() : nullptr;
      const bool wantsKeyboard = io && io->WantCaptureKeyboard;
      const bool wantsMouse = io && io->WantCaptureMouse;

      if (!wantsKeyboard) {
        ui.handleHotkeys(*input);
      }
      camera->aspect = static_cast<float>(window->getWidth())
                       / static_cast<float>(window->getHeight());
      if (!wantsKeyboard && !wantsMouse) {
        rig.update(*input, clock.deltaTime());
      } else {
        camera->updateMatrices();
      }
      input->nextFrame();
    });

    loop.run();
    renderer->shutdown();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[scene_viewer] fatal: " << e.what() << "\n";
    return 2;
  }
}

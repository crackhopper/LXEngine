// REQ-019: default integration demo.
//
// Wires:
//   cdToWhereAssetsExist -> Window -> VulkanRenderer -> Scene -> EngineLoop
//   -> ImGui editor panels / overlay -> run().

#include "backend/vulkan/vulkan_renderer.hpp"
#include "core/editor/command_bus.hpp"
#include "core/editor/commands/builtin_commands.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/editor/viewport_overlay.hpp"
#include "core/gpu/engine_loop.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/visibility_mask.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include "camera_rig.hpp"
#include "scene_builder.hpp"
#include "ui_overlay.hpp"

#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

using LX_core::backend::VulkanRenderer;
using LX_core::gpu::EngineLoop;

namespace demo = LX_demo::scene_viewer;

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

usize hashBytes(const void *data, usize size) {
  constexpr usize kFnvOffset = static_cast<usize>(1469598103934665603ull);
  constexpr usize kFnvPrime = static_cast<usize>(1099511628211ull);
  usize hash = kFnvOffset;
  const auto *bytes = static_cast<const std::uint8_t *>(data);
  for (usize i = 0; i < size; ++i) {
    hash ^= static_cast<usize>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

void logCameraAspectState(const char *label, int width, int height, float aspect,
                          const LX_core::CameraComponent &camera,
                          bool active) {
  if (!expSceneViewerDebugEnabled()) {
    return;
  }

  struct CameraLogState {
    int width = -1;
    int height = -1;
    bool finiteAspect = true;
    bool active = false;
    float eyeX = 0.0f;
    float eyeY = 0.0f;
    float eyeZ = 0.0f;
    float forwardX = 0.0f;
    float forwardY = 0.0f;
    float forwardZ = 0.0f;
    usize uboHash = 0;
  };
  static CameraLogState editorState{};
  static CameraLogState gameState{};
  CameraLogState &state =
      std::string_view(label) == "editor" ? editorState : gameState;
  const bool finiteAspect = std::isfinite(aspect);
  const LX_core::Vec3f eye = camera.getEyePosition();
  const LX_core::Vec3f forward = camera.getForwardVector();
  const auto ubo = camera.getUBO();
  const usize uboHash =
      ubo ? hashBytes(ubo->getRawData(), ubo->getByteSize()) : 0;
  if (state.width == width && state.height == height &&
      state.finiteAspect == finiteAspect && state.active == active &&
      state.eyeX == eye.x && state.eyeY == eye.y && state.eyeZ == eye.z &&
      state.forwardX == forward.x && state.forwardY == forward.y &&
      state.forwardZ == forward.z && state.uboHash == uboHash) {
    return;
  }
  state.width = width;
  state.height = height;
  state.finiteAspect = finiteAspect;
  state.active = active;
  state.eyeX = eye.x;
  state.eyeY = eye.y;
  state.eyeZ = eye.z;
  state.forwardX = forward.x;
  state.forwardY = forward.y;
  state.forwardZ = forward.z;
  state.uboHash = uboHash;

  std::cerr << "[scene_viewer][camera] " << label << " size=" << width << "x"
            << height << " aspect=" << aspect
            << " finiteAspect=" << finiteAspect
            << " active=" << active << " eye=(" << eye.x << ", " << eye.y
            << ", " << eye.z << ") forward=(" << forward.x << ", "
            << forward.y << ", " << forward.z << ") uboHash=" << uboHash;
  if (ubo) {
    const auto &p = ubo->param;
    std::cerr << " proj00=" << p.proj(0, 0) << " proj11=" << p.proj(1, 1)
              << " view03=" << p.view(0, 3)
              << " view13=" << p.view(1, 3)
              << " view23=" << p.view(2, 3);
  }
  std::cerr << std::endl;
}

void logActiveCameraState(bool previewEnabled,
                          const LX_core::SceneNodeSharedPtr &activeCamera) {
  if (!expSceneViewerDebugEnabled()) {
    return;
  }

  static bool lastPreviewEnabled = false;
  static std::string lastActivePath;
  const std::string activePath = activeCamera ? activeCamera->getPath() : "";
  if (lastPreviewEnabled == previewEnabled && lastActivePath == activePath) {
    return;
  }
  lastPreviewEnabled = previewEnabled;
  lastActivePath = activePath;
  std::cerr << "[scene_viewer][active-camera] previewEnabled="
            << previewEnabled << " activePath="
            << (activePath.empty() ? "<none>" : activePath) << std::endl;
}

} // namespace

int main() {
  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "[scene_viewer] failed to initialize runtime asset root\n";
    return 1;
  }

  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>(
        "demo_scene_viewer", kWindowWidth, kWindowHeight);

    auto vulkanRenderer = std::make_shared<VulkanRenderer>(VulkanRenderer::Token{});
    LX_core::gpu::RendererSharedPtr renderer = vulkanRenderer;
    renderer->initialize(window, "demo_scene_viewer");

    const std::filesystem::path gltfPath =
        resolveRuntimePath("assets/models/damaged_helmet/DamagedHelmet.gltf");
    auto helmet = demo::buildHelmetNode(gltfPath);
    auto ground = demo::buildGroundNode();
    helmet->setName("helmet");
    ground->setName("ground");
    helmet->setParent(ground);

    auto scene = LX_core::Scene::create("scene_viewer", helmet);
    scene->addRenderable(ground);

    auto lightNode = LX_core::SceneNode::create("dir_light_node");
    lightNode->setName("dir_light");
    scene->addRenderable(lightNode);

    auto editorCameraNode = LX_core::SceneNode::create("editor_camera");
    editorCameraNode->setName("editor_cam");
    auto editorCamera = editorCameraNode->addComponent<LX_core::CameraComponent>();
    if (!editorCamera.has_value()) {
      throw std::runtime_error("[scene_viewer] failed to create editor camera component");
    }
    editorCamera->get().aspect = static_cast<float>(kWindowWidth)
                                 / static_cast<float>(kWindowHeight);
    editorCamera->get().setTarget(LX_core::RenderTarget{});
    editorCamera->get().setCullingMask(LX_core::Layer_All);
    editorCamera->get().lookAt(LX_core::Vec3f{2.5f, 1.5f, 3.0f},
                               LX_core::Vec3f{0.0f, 0.0f, 0.0f},
                               LX_core::Vec3f{0.0f, 1.0f, 0.0f});
    editorCamera->get().updateMatrices();
    scene->addCamera(editorCameraNode);

    auto gameCameraNode = LX_core::SceneNode::create("game_camera");
    gameCameraNode->setName("game_cam");
    auto gameCamera = gameCameraNode->addComponent<LX_core::CameraComponent>();
    if (!gameCamera.has_value()) {
      throw std::runtime_error("[scene_viewer] failed to create game camera component");
    }
    gameCamera->get().aspect = static_cast<float>(kWindowWidth)
                               / static_cast<float>(kWindowHeight);
    gameCamera->get().setTarget(LX_core::RenderTarget{});
    gameCamera->get().setCullingMask(LX_core::Layer_All & ~LX_core::Layer_EditorOverlay);
    gameCamera->get().lookAt(LX_core::Vec3f{0.0f, 2.0f, 6.0f},
                             LX_core::Vec3f{0.0f, 0.0f, 0.0f},
                             LX_core::Vec3f{0.0f, 1.0f, 0.0f});
    gameCamera->get().updateMatrices();
    scene->addCamera(gameCameraNode);

    auto dirLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
        scene->getLights().front());
    if (dirLight && dirLight->ubo) {
      dirLight->ubo->param.dir = LX_core::Vec4f{-0.3f, -1.0f, -0.5f, 0.0f};
      dirLight->ubo->param.color = LX_core::Vec4f{1.0f, 0.98f, 0.9f, 1.0f};
      dirLight->ubo->setDirty();
    }

    demo::CameraRig rig;
    rig.attach(editorCamera->get());

    LX_core::EditorState editorState;
    editorState.setEditorCamera(editorCameraNode);
    editorState.setPreviewCamera(gameCameraNode);
    editorState.setPreviewEnabled(false);
    (void)editorState.syncActiveCamera(*scene);
    logActiveCameraState(editorState.isPreviewEnabled(),
                         editorState.resolveActiveCamera(*scene));

    LX_core::CommandBus commandBus;
    LX_core::registerBuiltinCommands(commandBus, editorState, *scene);
    LX_core::ConsolePanel consolePanel(commandBus);
    LX_core::SceneTreePanel sceneTreePanel(commandBus, editorState, *scene);
    LX_core::InspectorPanel inspectorPanel(commandBus, editorState);
    LX_core::ViewportOverlay viewportOverlay(commandBus, editorState, *scene);

    demo::UiOverlay ui;
    ui.attach(rig, commandBus, sceneTreePanel, inspectorPanel, consolePanel,
              viewportOverlay);

    vulkanRenderer->setDrawUiCallback([&] { ui.drawFrame(); });

    EngineLoop loop;
    loop.initialize(window, renderer);
    loop.startScene(scene);

    ui.attachClock(loop.getClock());

    auto input = window->getInputState();

    loop.setUpdateHook([&](LX_core::Scene& sceneRef, const LX_core::Clock& clock) {
      const bool imguiReady = ImGui::GetCurrentContext() != nullptr;
      const auto io =
          imguiReady
              ? std::optional<std::reference_wrapper<const ImGuiIO>>(
                    std::cref(ImGui::GetIO()))
              : std::nullopt;
      const bool wantsKeyboard = io && io->get().WantCaptureKeyboard;
      const bool wantsMouse = io && io->get().WantCaptureMouse;

      if (!wantsKeyboard) {
        ui.handleHotkeys(*input);
      }

      viewportOverlay.enqueueDebugDraw();

      const int windowWidth = window->getWidth();
      const int windowHeight = window->getHeight();
      static bool reportedZeroHeight = false;
      if (expSceneViewerDebugEnabled() && windowHeight <= 0 && !reportedZeroHeight) {
        std::cerr << "[scene_viewer][resize] zero-height frame size="
                  << windowWidth << "x" << windowHeight
                  << " previewEnabled=" << editorState.isPreviewEnabled()
                  << std::endl;
        reportedZeroHeight = true;
      } else if (windowHeight > 0) {
        reportedZeroHeight = false;
      }
      const bool hasValidExtent = windowWidth > 0 && windowHeight > 0;
      const float aspect =
          hasValidExtent
              ? static_cast<float>(windowWidth) / static_cast<float>(windowHeight)
              : editorCamera->get().aspect;
      if (hasValidExtent) {
        editorCamera->get().aspect = aspect;
        gameCamera->get().aspect = aspect;
      }
      gameCamera->get().updateMatrices();
      logActiveCameraState(editorState.isPreviewEnabled(),
                           editorState.resolveActiveCamera(sceneRef));
      logCameraAspectState("game", windowWidth, windowHeight, aspect,
                           gameCamera->get(), gameCamera->get().isActive());

      if (!wantsKeyboard && !wantsMouse) {
        rig.update(*input, clock.deltaTime());
      } else {
        editorCamera->get().updateMatrices();
      }
      logCameraAspectState("editor", windowWidth, windowHeight, aspect,
                           editorCamera->get(), editorCamera->get().isActive());
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

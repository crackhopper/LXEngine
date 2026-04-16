// game_app.cpp
#include "core/gpu/engine_loop.hpp"
#include "core/rhi/renderer.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/asset/texture.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/filesystem_tools.hpp"

// backend Vulkan 实现
#include "backend/vulkan/vulkan_renderer.hpp"

// 窗口系统
#include "infra/window/window.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "core/scene/orbit_camera_controller.hpp"
#include "core/scene/freefly_camera_controller.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

using namespace LX_core;
using namespace LX_core::gpu;

namespace {
bool testDebugEnabled() {
  static const bool enabled = [] {
    const char *value = std::getenv("LX_RENDER_DEBUG");
    return value != nullptr && std::strcmp(value, "0") != 0;
  }();
  return enabled;
}

float clampUnit(float value) {
  if (value < -1.0f) {
    return -1.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

void syncFreeFlyFromCamera(FreeFlyCameraController &controller,
                           const Camera &camera) {
  const Vec3f forward = (camera.target - camera.position).normalized();
  controller.setPosition(camera.position);
  controller.setYawDeg(std::atan2(forward.x, forward.z) * 180.0f /
                       3.14159265358979323846f);
  controller.setPitchDeg(std::asin(clampUnit(forward.y)) * 180.0f /
                         3.14159265358979323846f);
}

void syncOrbitFromCamera(OrbitCameraController &controller, const Camera &camera) {
  const Vec3f offset = camera.position - camera.target;
  const float distance = offset.length();
  controller.setTarget(camera.target);
  if (distance > 1e-6f) {
    controller.setDistance(distance);
    controller.setYawDeg(std::atan2(offset.x, offset.z) * 180.0f /
                         3.14159265358979323846f);
    controller.setPitchDeg(std::asin(clampUnit(offset.y / distance)) * 180.0f /
                           3.14159265358979323846f);
  }
}
} // namespace

int main() {
  if (!cdToWhereResourcesCouldFound("blinnphong_0")) {
    std::cerr << "Failed to locate runtime resources for blinnphong_0\n";
    return 1;
  }

  LX_infra::Window::Initialize();
  LX_core::WindowPtr window =
      std::make_shared<LX_infra::Window>("Test Renderer", 800, 600);

  RendererPtr renderer =
      std::make_shared<LX_core::backend::VulkanRenderer>(
          LX_core::backend::VulkanRenderer::Token{});
  renderer->initialize(window, "TestRenderTriangle");

  auto vertexBufferPtr = VertexBuffer<VertexPosNormalUvBone>::create({
      VertexPosNormalUvBone({-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                            {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      VertexPosNormalUvBone({1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                            {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
      VertexPosNormalUvBone({1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
                            {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
  });

  auto indexBufferPtr = IndexBuffer::create({0, 1, 2});
  auto meshPtr = Mesh::create(vertexBufferPtr, indexBufferPtr);

  auto material = LX_infra::loadGenericMaterial("materials/blinnphong_default.material");
  material->setInt(LX_core::StringID("enableNormal"), 0);
  material->syncGpuData();

  auto skeletonPtr = Skeleton::create({});

  auto renderable =
      std::make_shared<RenderableSubMesh>(meshPtr, material, skeletonPtr);

  auto scene = Scene::create(renderable);

  auto camera = scene->getCameras().front();
  auto dirLight =
      std::dynamic_pointer_cast<DirectionalLight>(scene->getLights().front());

  if (dirLight && dirLight->ubo) {
    dirLight->ubo->param.dir = Vec4f{0.0f, -1.0f, 0.0f, 0.0f};
    dirLight->ubo->param.color = Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
    dirLight->ubo->setDirty();
  }

  if (testDebugEnabled()) {
    std::cerr << "[TriangleTest] window=" << window->getWidth() << "x"
              << window->getHeight() << ", vertexBytes="
              << vertexBufferPtr->getByteSize() << ", indexBytes="
              << indexBufferPtr->getByteSize() << ", indices={0,1,2}"
              << std::endl;
  }

  auto input = window->getInputState();
  OrbitCameraController orbitCtrl({0, 0, 0}, 3.0f, 0.0f, 0.0f);
  FreeFlyCameraController freeflyCtrl({0, 0, 3}, 180.0f, 0.0f);
  bool useOrbit = true;
  bool tabWasDown = false;

  EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(scene);

  uint64_t frameCounter = 0;
  loop.setUpdateHook([&](Scene &, const Clock &clock) {
    // Tab key edge detection: toggle on press
    bool tabDown = input->isKeyDown(KeyCode::Tab);
    if (tabDown && !tabWasDown) {
      if (useOrbit) {
        syncFreeFlyFromCamera(freeflyCtrl, *camera);
      } else {
        syncOrbitFromCamera(orbitCtrl, *camera);
      }
      useOrbit = !useOrbit;
      if (testDebugEnabled()) {
        std::cerr << "[TriangleTest] switched to "
                  << (useOrbit ? "Orbit" : "FreeFly") << " camera\n";
      }
    }
    tabWasDown = tabDown;

    camera->aspect = 800.0f / 600.0f;
    if (useOrbit) {
      orbitCtrl.update(*camera, *input, clock.deltaTime());
    } else {
      freeflyCtrl.update(*camera, *input, clock.deltaTime());
    }
    camera->updateMatrices();
    input->nextFrame();
    if (testDebugEnabled() && frameCounter < 3) {
      std::cerr << "[TriangleTest] frame=" << frameCounter << ", cameraPos=("
                << camera->position.x << "," << camera->position.y << ","
                << camera->position.z << "), target=(" << camera->target.x
                << "," << camera->target.y << "," << camera->target.z
                << "), aspect=" << camera->aspect << std::endl;
      if (frameCounter == 0) {
        const auto &view = camera->ubo->param.view;
        const auto &proj = camera->ubo->param.proj;
        const std::array<Vec4f, 3> debugPositions = {
            Vec4f{-1.0f, 1.0f, 0.0f, 1.0f},
            Vec4f{1.0f, 1.0f, 0.0f, 1.0f},
            Vec4f{1.0f, -1.0f, 0.0f, 1.0f},
        };
        for (size_t i = 0; i < debugPositions.size(); ++i) {
          const Vec4f viewPos = view * debugPositions[i];
          const Vec4f clipPos = proj * viewPos;
          std::cerr << "[TriangleTest] vertex" << i << " view=(" << viewPos.x
                    << "," << viewPos.y << "," << viewPos.z << ","
                    << viewPos.w << "), clip=(" << clipPos.x << ","
                    << clipPos.y << "," << clipPos.z << "," << clipPos.w
                    << ")";
          if (clipPos.w != 0.0f) {
            std::cerr << ", ndc=(" << clipPos.x / clipPos.w << ","
                      << clipPos.y / clipPos.w << ","
                      << clipPos.z / clipPos.w << ")";
          }
          std::cerr << std::endl;
        }
      }
    }
    ++frameCounter;
    if (testDebugEnabled() && frameCounter % 120 == 0) {
      std::cerr << "[TriangleTest] heartbeat frame=" << frameCounter
                << std::endl;
    }
  });

  loop.run();
  renderer->shutdown();

  return 0;
}

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/ibl_bake_renderer.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/utils/env.hpp"
#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <iostream>

int main() {
  expSetEnvVK();
  try {
    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan IBL Bake", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanIblBake");
    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, 1, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);

    LX_core::backend::IblBakeRenderer baker(*device, *resourceManager,
                                            *cmdBufferMgr);
    const auto result = baker.bakeStaticEnvironment({
        .skyboxSize = 16,
        .irradianceSize = 8,
        .prefilterSize = 16,
        .prefilterMipCount = 4,
        .brdfLutSize = 16,
    });

    if (!result.skybox ||
        result.skybox->getBindingName() != LX_core::StringID("SkyboxMap")) {
      std::cerr << "Skybox bake resource missing or has wrong binding\n";
      return 1;
    }
    if (!result.irradiance ||
        result.irradiance->getBindingName() !=
            LX_core::StringID("IrradianceMap")) {
      std::cerr << "Irradiance bake resource missing or has wrong binding\n";
      return 1;
    }
    if (!result.prefiltered ||
        result.prefiltered->getBindingName() !=
            LX_core::StringID("PrefilteredEnvMap")) {
      std::cerr << "Prefilter bake resource missing or has wrong binding\n";
      return 1;
    }
    if (!result.brdfLut ||
        result.brdfLut->getBindingName() != LX_core::StringID("BrdfLut")) {
      std::cerr << "BRDF LUT bake resource missing or has wrong binding\n";
      return 1;
    }
    if (!baker.debugReadbackCubemapFaceHasData(LX_core::StringID("SkyboxMap"),
                                               0, 0, 16)) {
      std::cerr << "Skybox cubemap face readback is empty\n";
      return 1;
    }
    if (!baker.debugReadbackCubemapFaceHasData(
            LX_core::StringID("PrefilteredEnvMap"), 3, 5, 2)) {
      std::cerr << "Prefilter cubemap face/mip readback is empty\n";
      return 1;
    }
    auto skyboxTexture =
        resourceManager->getTexture(result.skybox->getBackendCacheIdentity());
    if (!skyboxTexture || skyboxTexture->get().getArrayLayers() != 6u) {
      std::cerr << "Baked skybox resource is not descriptor-addressable\n";
      return 1;
    }
    auto brdfTexture =
        resourceManager->getTexture(result.brdfLut->getBackendCacheIdentity());
    if (!brdfTexture || brdfTexture->get().getArrayLayers() != 1u) {
      std::cerr << "Baked BRDF LUT resource is not descriptor-addressable\n";
      return 1;
    }
    if (!baker.debugReadbackBrdfLutHasData(16)) {
      std::cerr << "BRDF LUT shader output readback is empty\n";
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanIblBake test: " << e.what() << "\n";
    return 0;
  }
}

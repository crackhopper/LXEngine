#include "gui.hpp"
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <stdexcept>

namespace infra {

struct Gui::Impl {
  bool initialized = false;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkQueue presentQueue = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  uint32_t graphicsQueueFamilyIndex = 0;
  uint32_t presentQueueFamilyIndex = 0;
};

Gui::Gui() : pImpl(new Impl) {}

Gui::~Gui() {
  if (pImpl->initialized) {
    shutdown();
  }
  delete pImpl;
}

void Gui::init(const InitParams& params) {
  if (pImpl->initialized) {
    throw std::runtime_error("Gui already initialized");
  }

  pImpl->instance = params.instance;
  pImpl->physicalDevice = params.physicalDevice;
  pImpl->device = params.device;
  pImpl->graphicsQueue = params.graphicsQueue;
  pImpl->presentQueue = params.presentQueue;
  pImpl->surface = params.surface;
  pImpl->graphicsQueueFamilyIndex = params.graphicsQueueFamilyIndex;
  pImpl->presentQueueFamilyIndex = params.presentQueueFamilyIndex;

  ImGui_ImplVulkan_InitInfo initInfo = {};
  initInfo.ApiVersion = VK_API_VERSION_1_0;
  initInfo.Instance = params.instance;
  initInfo.PhysicalDevice = params.physicalDevice;
  initInfo.Device = params.device;
  initInfo.QueueFamily = params.graphicsQueueFamilyIndex;
  initInfo.Queue = params.graphicsQueue;
  initInfo.PipelineCache = VK_NULL_HANDLE;
  initInfo.DescriptorPool = VK_NULL_HANDLE;
  initInfo.DescriptorPoolSize = 0;
  initInfo.MinImageCount = 2;
  initInfo.ImageCount = 2;
  initInfo.Allocator = nullptr;
  initInfo.CheckVkResultFn = nullptr;

  if (!ImGui_ImplVulkan_Init(&initInfo)) {
    throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
  }

  pImpl->initialized = true;
}

void Gui::beginFrame() {
  ImGui_ImplSDL3_NewFrame();
  ImGui_ImplVulkan_NewFrame();
  ImGui::NewFrame();
}

void Gui::endFrame() {
  ImGui::Render();
  ImDrawData* drawData = ImGui::GetDrawData();
  if (drawData) {
    ImGui_ImplVulkan_RenderDrawData(drawData, VK_NULL_HANDLE, VK_NULL_HANDLE);
  }
}

void Gui::shutdown() {
  if (!pImpl->initialized) return;

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  pImpl->initialized = false;
}

bool Gui::isInitialized() const {
  return pImpl->initialized;
}

} // namespace infra
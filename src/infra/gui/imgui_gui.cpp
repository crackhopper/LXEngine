#include "gui.hpp"
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <array>
#include <stdexcept>

#ifdef USE_SDL
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#elif defined(USE_GLFW)
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#endif

namespace infra {

struct Gui::Impl {
  bool initialized = false;
  VkDevice device = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  usize swapchainImageCount = 0;
};

Gui::Gui() : pImpl(std::make_unique<Impl>()) {}

Gui::~Gui() {
  if (pImpl->initialized) {
    shutdown();
  }
}

namespace {

VkDescriptorPool createImGuiDescriptorPool(VkDevice device) {
  constexpr u32 kPoolSize = 1000;
  const std::array<VkDescriptorPoolSize, 11> poolSizes = {{
      {VK_DESCRIPTOR_TYPE_SAMPLER, kPoolSize},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kPoolSize},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kPoolSize},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kPoolSize},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kPoolSize},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kPoolSize},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kPoolSize},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kPoolSize},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kPoolSize},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kPoolSize},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kPoolSize},
  }};

  VkDescriptorPoolCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  info.maxSets = kPoolSize * static_cast<u32>(poolSizes.size());
  info.poolSizeCount = static_cast<u32>(poolSizes.size());
  info.pPoolSizes = poolSizes.data();

  VkDescriptorPool pool = VK_NULL_HANDLE;
  if (vkCreateDescriptorPool(device, &info, nullptr, &pool) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create ImGui descriptor pool");
  }
  return pool;
}

} // namespace

void Gui::init(const InitParams& params) {
  if (pImpl->initialized) {
    throw std::runtime_error("Gui already initialized");
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

#if defined(USE_SDL)
  if (!ImGui_ImplSDL3_InitForVulkan(
          static_cast<SDL_Window*>(params.nativeWindowHandle))) {
    ImGui::DestroyContext();
    throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
  }
#elif defined(USE_GLFW)
  if (!ImGui_ImplGlfw_InitForVulkan(
          static_cast<GLFWwindow*>(params.nativeWindowHandle), true)) {
    ImGui::DestroyContext();
    throw std::runtime_error("Failed to initialize ImGui GLFW backend");
  }
#else
  ImGui::DestroyContext();
  throw std::runtime_error("Gui requires USE_SDL or USE_GLFW");
#endif

  pImpl->device = params.device;
  pImpl->swapchainImageCount = params.swapchainImageCount;
  pImpl->descriptorPool = createImGuiDescriptorPool(params.device);

  ImGui_ImplVulkan_InitInfo initInfo = {};
  initInfo.ApiVersion = VK_API_VERSION_1_0;
  initInfo.Instance = params.instance;
  initInfo.PhysicalDevice = params.physicalDevice;
  initInfo.Device = params.device;
  initInfo.QueueFamily = static_cast<u32>(params.graphicsQueueFamilyIndex);
  initInfo.Queue = params.graphicsQueue;
  initInfo.PipelineCache = VK_NULL_HANDLE;
  initInfo.DescriptorPool = pImpl->descriptorPool;
  initInfo.DescriptorPoolSize = 0;
  initInfo.MinImageCount = static_cast<u32>(params.swapchainImageCount);
  initInfo.ImageCount = static_cast<u32>(params.swapchainImageCount);
  initInfo.PipelineInfoMain.RenderPass = params.renderPass;
  initInfo.PipelineInfoMain.Subpass = 0;
  initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  initInfo.Allocator = nullptr;
  initInfo.CheckVkResultFn = nullptr;

  if (!ImGui_ImplVulkan_Init(&initInfo)) {
    vkDestroyDescriptorPool(pImpl->device, pImpl->descriptorPool, nullptr);
    pImpl->descriptorPool = VK_NULL_HANDLE;
#if defined(USE_SDL)
    ImGui_ImplSDL3_Shutdown();
#elif defined(USE_GLFW)
    ImGui_ImplGlfw_Shutdown();
#endif
    ImGui::DestroyContext();
    throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
  }

  pImpl->initialized = true;
}

void Gui::beginFrame() {
  if (!pImpl->initialized) return;
#if defined(USE_SDL)
  ImGui_ImplSDL3_NewFrame();
#elif defined(USE_GLFW)
  ImGui_ImplGlfw_NewFrame();
#endif
  ImGui_ImplVulkan_NewFrame();
  ImGui::NewFrame();
}

void Gui::endFrame(VkCommandBuffer cmd) {
  if (!pImpl->initialized) return;
  ImGui::Render();
  ImDrawData* drawData = ImGui::GetDrawData();
  if (!drawData || drawData->TotalVtxCount == 0) {
    return;
  }
  ImGui_ImplVulkan_RenderDrawData(drawData, cmd, VK_NULL_HANDLE);
}

void Gui::updateSwapchainImageCount(usize imageCount) {
  if (!pImpl->initialized || imageCount == 0 ||
      imageCount == pImpl->swapchainImageCount) {
    return;
  }
  ImGui_ImplVulkan_SetMinImageCount(static_cast<u32>(imageCount));
  pImpl->swapchainImageCount = imageCount;
}

void Gui::shutdown() {
  if (!pImpl->initialized) return;

  ImGui_ImplVulkan_Shutdown();
#if defined(USE_SDL)
  ImGui_ImplSDL3_Shutdown();
#elif defined(USE_GLFW)
  ImGui_ImplGlfw_Shutdown();
#endif
  if (pImpl->descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(pImpl->device, pImpl->descriptorPool, nullptr);
    pImpl->descriptorPool = VK_NULL_HANDLE;
  }
  ImGui::DestroyContext();

  pImpl->initialized = false;
  pImpl->swapchainImageCount = 0;
}

bool Gui::isInitialized() const {
  return pImpl->initialized;
}

} // namespace infra

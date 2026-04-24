#pragma once
#include "core/platform/types.hpp"
#include <vulkan/vulkan.h>
#include <functional>

namespace infra {

class Gui {
public:
  struct InitParams {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    QueueFamilyIndex32 graphicsQueueFamilyIndex;
    QueueFamilyIndex32 presentQueueFamilyIndex;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
    // SDL path: must be an SDL_Window*. GLFW path: GLFWwindow*.
    void* nativeWindowHandle;
    VkRenderPass renderPass;
    ImageCount swapchainImageCount;
  };

  Gui();
  ~Gui();

  void init(const InitParams& params);
  void beginFrame();
  void endFrame(VkCommandBuffer cmd);
  void updateSwapchainImageCount(ImageCount imageCount);
  void shutdown();

  bool isInitialized() const;

private:
  struct Impl;
  Impl* pImpl;
};

} // namespace infra

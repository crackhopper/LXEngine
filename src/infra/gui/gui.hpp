#pragma once
#include <vulkan/vulkan.h>
#include <functional>

namespace infra {

class Gui {
public:
  struct InitParams {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t graphicsQueueFamilyIndex;
    uint32_t presentQueueFamilyIndex;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
  };

  Gui();
  ~Gui();

  void init(const InitParams& params);
  void beginFrame();
  void endFrame();
  void shutdown();

  bool isInitialized() const;

private:
  struct Impl;
  Impl* pImpl;
};

} // namespace infra
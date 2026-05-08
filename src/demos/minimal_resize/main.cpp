// REQ-DIAG-MIN-DELTA2-ONLY: single-variable isolation. delta 2 + 3 +
// 4 + 5 stacked produced resize black-screen on the user's hardware
// (NVIDIA RTX 3070 Ti Laptop, dGPU). The user's machine is a laptop
// with an Intel iGPU + NVIDIA dGPU; baseline (first-suitable) almost
// certainly selected the Intel iGPU, while delta 2 (score-based pick)
// switched to the NVIDIA dGPU — a path known to have driver / loader
// issues with Vulkan WSI resize on Optimus laptops.
//
// This iteration: keep ONLY delta 2 (score-based pick), revert
// deltas 1/3/4/5 back to the frozen-baseline behaviour. If this demo
// still black-screens, the trigger is unambiguously the GPU selection
// (i.e. running on the dGPU vs the iGPU). If it stops, one of the
// other deltas is involved together with delta 2.
//
// Also enumerates and prints every available physical device so we
// can confirm what GPUs the machine exposes and which one each
// strategy ends up picking.

#include "core/platform/types.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kMaxFramesInFlight = 2;

struct Vertex {
  float pos[3];
  float color[3];

  static VkVertexInputBindingDescription getBindingDescription() {
    VkVertexInputBindingDescription b{};
    b.binding = 0;
    b.stride = sizeof(Vertex);
    b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return b;
  }

  static std::array<VkVertexInputAttributeDescription, 2>
  getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 2> a{};
    a[0].binding = 0;
    a[0].location = 0;
    a[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    a[0].offset = offsetof(Vertex, pos);
    a[1].binding = 0;
    a[1].location = 1;
    a[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    a[1].offset = offsetof(Vertex, color);
    return a;
  }
};

const std::vector<Vertex> kVertices = {
    {{-0.5f, -0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, -0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    {{-0.7f, -0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
    {{0.7f, -0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
    {{0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
    {{-0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
};

const std::vector<u32> kIndices = {
    0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4,
};

const std::vector<const char *> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

struct QueueFamilyIndices {
  std::optional<u32> graphicsFamily;
  std::optional<u32> presentFamily;
  bool isComplete() const {
    return graphicsFamily.has_value() && presentFamily.has_value();
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities{};
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

const char *deviceTypeToString(VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return "integrated";
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return "discrete";
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return "virtual";
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return "cpu";
  case VK_PHYSICAL_DEVICE_TYPE_OTHER:
  default:
    return "other";
  }
}

// LX_VK_PREFER_GPU env override:
//   unset / "discrete"  → discrete GPU wins on dual-GPU laptops (default).
//   "integrated"        → integrated GPU outranks discrete.
// Used to A/B test the same demo binary on iGPU vs dGPU without rebuilding.
int physicalDevicePreferenceScore(VkPhysicalDeviceType type) {
  const char *pref = std::getenv("LX_VK_PREFER_GPU");
  const bool preferIntegrated = pref && std::string(pref) == "integrated";

  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return preferIntegrated ? 3 : 4;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return preferIntegrated ? 4 : 3;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return 2;
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return 1;
  case VK_PHYSICAL_DEVICE_TYPE_OTHER:
  default:
    return 0;
  }
}

std::vector<u32> readShaderCode(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader file: " + path.string());
  }
  const auto size = static_cast<usize>(file.tellg());
  if (size % sizeof(u32) != 0) {
    throw std::runtime_error("Shader bytecode size not 4-byte aligned");
  }
  std::vector<u32> code(size / sizeof(u32));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(code.data()),
            static_cast<std::streamsize>(size));
  return code;
}

class HelloTriangleApp {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  std::shared_ptr<LX_infra::Window> m_window;

  VkInstance m_instance = VK_NULL_HANDLE;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  VkQueue m_graphicsQueue = VK_NULL_HANDLE;
  VkQueue m_presentQueue = VK_NULL_HANDLE;
  u32 m_graphicsFamily = 0;
  u32 m_presentFamily = 0;
  VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

  VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
  std::vector<VkImage> m_swapChainImages;
  VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D m_swapChainExtent{};
  std::vector<VkImageView> m_swapChainImageViews;
  std::vector<VkFramebuffer> m_swapChainFramebuffers;

  VkRenderPass m_renderPass = VK_NULL_HANDLE;
  VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

  VkCommandPool m_commandPool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> m_commandBuffers;

  VkImage m_depthImage = VK_NULL_HANDLE;
  VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
  VkImageView m_depthImageView = VK_NULL_HANDLE;

  VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
  VkBuffer m_indexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;

  std::vector<VkSemaphore> m_imageAvailableSemaphores;
  std::vector<VkSemaphore> m_renderFinishedSemaphores;
  std::vector<VkFence> m_inFlightFences;
  u32 m_currentFrame = 0;

  void initWindow() {
    LX_infra::Window::Initialize();
    m_window = std::make_shared<LX_infra::Window>(
        "demo_minimal_resize", kWindowWidth, kWindowHeight);
  }

  void initVulkan() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createCommandPool();
    createDepthResources();
    createFramebuffers();
    createVertexBuffer();
    createIndexBuffer();
    createCommandBuffers();
    createSyncObjects();
  }

  void mainLoop() {
    while (!m_window->shouldClose()) {
      if (m_window->getWidth() <= 0 || m_window->getHeight() <= 0) {
        continue;
      }
      drawFrame();
    }
    vkDeviceWaitIdle(m_device);
  }

  void createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "demo_minimal_resize";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char *> extensions;
    m_window->getRequiredExtensions(extensions);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create Vulkan instance");
    }
  }

  // Reverted to baseline: getVulkanSurface (delta 5 dropped).
  void createSurface() { m_surface = m_window->getVulkanSurface(m_instance); }

  // === DELTA 2 (the only delta kept this round): score-based pick ===
  void pickPhysicalDevice() {
    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
      throw std::runtime_error("No Vulkan-capable physical device");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Diagnostic: enumerate every device the loader returned.
    std::cout << "[minimal_resize] Available physical devices ("
              << deviceCount << "):\n";
    for (u32 i = 0; i < deviceCount; ++i) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(devices[i], &props);
      const bool suitable = isDeviceSuitable(devices[i]);
      std::cout << "  [" << i << "] " << deviceTypeToString(props.deviceType)
                << " — " << props.deviceName
                << "  (suitable=" << (suitable ? "yes" : "no")
                << ", score=" << physicalDevicePreferenceScore(props.deviceType)
                << ")\n";
    }

    int bestScore = -1;
    for (const auto &device : devices) {
      if (!isDeviceSuitable(device)) {
        continue;
      }
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(device, &props);
      const int score = physicalDevicePreferenceScore(props.deviceType);
      if (score > bestScore) {
        bestScore = score;
        m_physicalDevice = device;
      }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
      throw std::runtime_error("No suitable physical device");
    }
    m_depthFormat = findDepthFormat();

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    std::cout << "Selected " << deviceTypeToString(properties.deviceType)
              << " GPU: " << properties.deviceName << std::endl;
    std::cout << "  Driver version: " << properties.driverVersion << std::endl;
    std::cout << "  Vulkan API: " << VK_VERSION_MAJOR(properties.apiVersion)
              << "." << VK_VERSION_MINOR(properties.apiVersion) << "."
              << VK_VERSION_PATCH(properties.apiVersion) << std::endl;
  }

  // Reverted to baseline: keep swapChainAdequate check (delta 4 dropped).
  bool isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    bool swapChainAdequate = false;
    if (extensionsSupported) {
      SwapChainSupportDetails support = querySwapChainSupport(device);
      swapChainAdequate =
          !support.formats.empty() && !support.presentModes.empty();
    }
    return indices.isComplete() && extensionsSupported && swapChainAdequate;
  }

  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    for (u32 i = 0; i < count; ++i) {
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphicsFamily = i;
      }
      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface,
                                           &presentSupport);
      if (presentSupport == VK_TRUE) {
        indices.presentFamily = i;
      }
      if (indices.isComplete()) {
        break;
      }
    }
    return indices;
  }

  bool checkDeviceExtensionSupport(VkPhysicalDevice device) const {
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                         available.data());
    std::set<std::string> required(kDeviceExtensions.begin(),
                                   kDeviceExtensions.end());
    for (const auto &ext : available) {
      required.erase(ext.extensionName);
    }
    return required.empty();
  }

  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface,
                                              &details.capabilities);
    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount,
                                         nullptr);
    if (formatCount > 0) {
      details.formats.resize(formatCount);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount,
                                           details.formats.data());
    }
    u32 modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &modeCount,
                                              nullptr);
    if (modeCount > 0) {
      details.presentModes.resize(modeCount);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &modeCount,
                                                details.presentModes.data());
    }
    return details;
  }

  VkFormat findDepthFormat() const {
    std::array<VkFormat, 3> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (VkFormat f : candidates) {
      VkFormatProperties props;
      vkGetPhysicalDeviceFormatProperties(m_physicalDevice, f, &props);
      if (props.optimalTilingFeatures &
          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return f;
      }
    }
    throw std::runtime_error("No supported depth format");
  }

  void createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    m_graphicsFamily = *indices.graphicsFamily;
    m_presentFamily = *indices.presentFamily;

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<u32> uniqueFamilies = {m_graphicsFamily, m_presentFamily};
    float priority = 1.0f;
    for (u32 family : uniqueFamilies) {
      VkDeviceQueueCreateInfo q{};
      q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      q.queueFamilyIndex = family;
      q.queueCount = 1;
      q.pQueuePriorities = &priority;
      queueCreateInfos.push_back(q);
    }

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount =
        static_cast<u32>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount =
        static_cast<u32>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create logical device");
    }
    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
  }

  // Reverted to baseline: only B8G8R8A8_SRGB (delta 3 dropped).
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &available) const {
    for (const auto &f : available) {
      if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return f;
      }
    }
    return available[0];
  }

  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR> &available) const {
    // LX_VK_PRESENT_MODE env override:
    //   "fifo"      → force VK_PRESENT_MODE_FIFO_KHR
    //   "immediate" → force VK_PRESENT_MODE_IMMEDIATE_KHR (falls back if absent)
    //   unset / "mailbox" → MAILBOX-first, FIFO fallback (default)
    //
    // MAILBOX-first is the default because on NVIDIA Optimus laptops the
    // dGPU present path goes through a cross-GPU PRIME copy; FIFO's strict
    // 1-frame-per-vsync cadence makes that copy's timing window very tight
    // and is one of the suspected triggers of the resize black-screen.
    // MAILBOX lets the driver queue and drop frames, giving the PRIME link
    // more slack. dxvk / vkd3d-proton use the same fallback on Optimus.
    const char *modeOverride = std::getenv("LX_VK_PRESENT_MODE");
    const std::string pref = modeOverride ? modeOverride : "mailbox";

    auto has = [&](VkPresentModeKHR m) {
      for (auto mode : available) {
        if (mode == m) return true;
      }
      return false;
    };

    if (pref == "fifo") {
      std::cout << "[minimal_resize] present mode: VK_PRESENT_MODE_FIFO_KHR"
                   " (LX_VK_PRESENT_MODE=fifo)"
                << std::endl;
      return VK_PRESENT_MODE_FIFO_KHR;
    }
    if (pref == "immediate") {
      if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        std::cout << "[minimal_resize] present mode: "
                     "VK_PRESENT_MODE_IMMEDIATE_KHR"
                     " (LX_VK_PRESENT_MODE=immediate)"
                  << std::endl;
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
      }
      std::cout << "[minimal_resize] LX_VK_PRESENT_MODE=immediate but driver "
                   "doesn't expose it; falling back to MAILBOX/FIFO"
                << std::endl;
    }

    // Default ("mailbox" or unrecognized override): MAILBOX-first.
    if (has(VK_PRESENT_MODE_MAILBOX_KHR)) {
      std::cout << "[minimal_resize] present mode: VK_PRESENT_MODE_MAILBOX_KHR"
                << std::endl;
      return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    std::cout << "[minimal_resize] present mode: VK_PRESENT_MODE_FIFO_KHR"
                 " (MAILBOX not advertised by driver)"
              << std::endl;
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &cap) const {
    if (cap.currentExtent.width != std::numeric_limits<u32>::max()) {
      return cap.currentExtent;
    }
    VkExtent2D actual = {static_cast<u32>(m_window->getWidth()),
                         static_cast<u32>(m_window->getHeight())};
    actual.width = std::clamp(actual.width, cap.minImageExtent.width,
                              cap.maxImageExtent.width);
    actual.height = std::clamp(actual.height, cap.minImageExtent.height,
                               cap.maxImageExtent.height);
    return actual;
  }

  void createSwapChain() {
    SwapChainSupportDetails support = querySwapChainSupport(m_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = chooseSwapExtent(support.capabilities);

    u32 imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
      imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    u32 queueFamilyIndices[] = {m_graphicsFamily, m_presentFamily};
    if (m_graphicsFamily != m_presentFamily) {
      createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      createInfo.queueFamilyIndexCount = 2;
      createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
      createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create swapchain");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount,
                            m_swapChainImages.data());
    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;
  }

  VkImageView createImageView(VkImage image, VkFormat format,
                              VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspectFlags;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(m_device, &info, nullptr, &view) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create image view");
    }
    return view;
  }

  void createImageViews() {
    m_swapChainImageViews.resize(m_swapChainImages.size());
    for (usize i = 0; i < m_swapChainImages.size(); ++i) {
      m_swapChainImageViews[i] = createImageView(
          m_swapChainImages[i], m_swapChainImageFormat,
          VK_IMAGE_ASPECT_COLOR_BIT);
    }
  }

  void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
                                                          depthAttachment};
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<u32>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;

    if (vkCreateRenderPass(m_device, &info, nullptr, &m_renderPass) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create render pass");
    }
  }

  VkShaderModule createShaderModule(const std::vector<u32> &code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(u32);
    info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &info, nullptr, &module) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create shader module");
    }
    return module;
  }

  void createGraphicsPipeline() {
    const auto shaderDir = getRuntimeShaderBinaryDir();
    auto vertCode = readShaderCode(shaderDir / "minimal.vert.spv");
    auto fragCode = readShaderCode(shaderDir / "minimal.frag.spv");
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<u32>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamics = {VK_DYNAMIC_STATE_VIEWPORT,
                                              VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<u32>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<u32>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &m_graphicsPipeline) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(m_device, fragModule, nullptr);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
  }

  void createCommandPool() {
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = m_graphicsFamily;
    if (vkCreateCommandPool(m_device, &info, nullptr, &m_commandPool) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create command pool");
    }
  }

  u32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (u32 i = 0; i < memProps.memoryTypeCount; ++i) {
      if ((typeFilter & (1 << i)) &&
          (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }
    throw std::runtime_error("Failed to find memory type");
  }

  void createImage(u32 w, u32 h, VkFormat format, VkImageTiling tiling,
                   VkImageUsageFlags usage, VkMemoryPropertyFlags props,
                   VkImage &image, VkDeviceMemory &mem) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent.width = w;
    info.extent.height = h;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = format;
    info.tiling = tiling;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &info, nullptr, &image) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create image");
    }
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, image, &memReq);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, props);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &mem) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate image memory");
    }
    vkBindImageMemory(m_device, image, mem, 0);
  }

  void createDepthResources() {
    createImage(m_swapChainExtent.width, m_swapChainExtent.height, m_depthFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage,
                m_depthImageMemory);
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (m_depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        m_depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
      aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    m_depthImageView = createImageView(m_depthImage, m_depthFormat, aspect);
  }

  void createFramebuffers() {
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());
    for (usize i = 0; i < m_swapChainImageViews.size(); ++i) {
      std::array<VkImageView, 2> attachments = {m_swapChainImageViews[i],
                                                m_depthImageView};
      VkFramebufferCreateInfo info{};
      info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      info.renderPass = m_renderPass;
      info.attachmentCount = static_cast<u32>(attachments.size());
      info.pAttachments = attachments.data();
      info.width = m_swapChainExtent.width;
      info.height = m_swapChainExtent.height;
      info.layers = 1;
      if (vkCreateFramebuffer(m_device, &info, nullptr,
                              &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer");
      }
    }
  }

  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags props, VkBuffer &buffer,
                    VkDeviceMemory &mem) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &info, nullptr, &buffer) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create buffer");
    }
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, buffer, &memReq);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, props);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &mem) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate buffer memory");
    }
    vkBindBufferMemory(m_device, buffer, mem, 0);
  }

  void createVertexBuffer() {
    VkDeviceSize size = sizeof(Vertex) * kVertices.size();
    createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_vertexBuffer, m_vertexBufferMemory);
    void *data = nullptr;
    vkMapMemory(m_device, m_vertexBufferMemory, 0, size, 0, &data);
    std::memcpy(data, kVertices.data(), static_cast<usize>(size));
    vkUnmapMemory(m_device, m_vertexBufferMemory);
  }

  void createIndexBuffer() {
    VkDeviceSize size = sizeof(u32) * kIndices.size();
    createBuffer(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_indexBuffer, m_indexBufferMemory);
    void *data = nullptr;
    vkMapMemory(m_device, m_indexBufferMemory, 0, size, 0, &data);
    std::memcpy(data, kIndices.data(), static_cast<usize>(size));
    vkUnmapMemory(m_device, m_indexBufferMemory);
  }

  void createCommandBuffers() {
    m_commandBuffers.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = m_commandPool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = static_cast<u32>(m_commandBuffers.size());
    if (vkAllocateCommandBuffers(m_device, &info, m_commandBuffers.data()) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate command buffers");
    }
  }

  void createSyncObjects() {
    m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
    m_renderFinishedSemaphores.resize(kMaxFramesInFlight);
    m_inFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
      if (vkCreateSemaphore(m_device, &semInfo, nullptr,
                            &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
          vkCreateSemaphore(m_device, &semInfo, nullptr,
                            &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
          vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) !=
              VK_SUCCESS) {
        throw std::runtime_error("Failed to create sync objects");
      }
    }
  }

  void recordCommandBuffer(VkCommandBuffer cmd, u32 imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin command buffer");
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderPass;
    rpInfo.framebuffer = m_swapChainFramebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_swapChainExtent;
    rpInfo.clearValueCount = static_cast<u32>(clearValues.size());
    rpInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapChainExtent.width);
    viewport.height = static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_graphicsPipeline);

    VkBuffer vbuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbuffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, static_cast<u32>(kIndices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
      throw std::runtime_error("Failed to end command buffer");
    }
  }

  void cleanupSwapChain() {
    if (m_depthImageView != VK_NULL_HANDLE) {
      vkDestroyImageView(m_device, m_depthImageView, nullptr);
      m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
      vkDestroyImage(m_device, m_depthImage, nullptr);
      m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
      vkFreeMemory(m_device, m_depthImageMemory, nullptr);
      m_depthImageMemory = VK_NULL_HANDLE;
    }
    for (auto fb : m_swapChainFramebuffers) {
      vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    m_swapChainFramebuffers.clear();
    for (auto v : m_swapChainImageViews) {
      vkDestroyImageView(m_device, v, nullptr);
    }
    m_swapChainImageViews.clear();
    if (m_swapChain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
      m_swapChain = VK_NULL_HANDLE;
    }
  }

  void recreateSwapChain() {
    while (m_window->getWidth() <= 0 || m_window->getHeight() <= 0) {
      if (m_window->shouldClose()) {
        return;
      }
    }
    vkDeviceWaitIdle(m_device);
    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createDepthResources();
    createFramebuffers();
  }

  void drawFrame() {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE,
                    UINT64_MAX);

    u32 imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE,
        &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      recreateSwapChain();
      return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error("Failed to acquire swapchain image");
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSems[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSems;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];
    VkSemaphore signalSems[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSems;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo,
                      m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to submit");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSems;
    VkSwapchainKHR swapChains[] = {m_swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR ||
        result == VK_SUBOPTIMAL_KHR) {
      recreateSwapChain();
    } else if (result != VK_SUCCESS) {
      throw std::runtime_error("Failed to present");
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
  }

  void cleanup() {
    cleanupSwapChain();

    if (m_graphicsPipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    }
    if (m_renderPass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    }
    if (m_indexBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
    }
    if (m_indexBufferMemory != VK_NULL_HANDLE) {
      vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
      vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
    }
    for (auto s : m_imageAvailableSemaphores) {
      vkDestroySemaphore(m_device, s, nullptr);
    }
    for (auto s : m_renderFinishedSemaphores) {
      vkDestroySemaphore(m_device, s, nullptr);
    }
    for (auto f : m_inFlightFences) {
      vkDestroyFence(m_device, f, nullptr);
    }
    if (m_commandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }
    if (m_device != VK_NULL_HANDLE) {
      vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface != VK_NULL_HANDLE) {
      vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
      vkDestroyInstance(m_instance, nullptr);
    }
  }
};

} // namespace

int main() {
  expSetEnvVK();

  // Surface the GPU preference and present mode so the resolved choice
  // is obvious from stdout.
  if (const char *pref = std::getenv("LX_VK_PREFER_GPU")) {
    std::cout << "[minimal_resize] LX_VK_PREFER_GPU=" << pref << std::endl;
  } else {
    std::cout << "[minimal_resize] LX_VK_PREFER_GPU=(unset → discrete-first)"
              << std::endl;
  }
  if (const char *pmode = std::getenv("LX_VK_PRESENT_MODE")) {
    std::cout << "[minimal_resize] LX_VK_PRESENT_MODE=" << pmode << std::endl;
  } else {
    std::cout << "[minimal_resize] LX_VK_PRESENT_MODE=(unset → mailbox-first)"
              << std::endl;
  }

  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "[minimal_resize] failed to initialize runtime asset root\n";
    return 1;
  }

  try {
    HelloTriangleApp app;
    app.run();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[minimal_resize] fatal: " << e.what() << "\n";
    return 2;
  }
}

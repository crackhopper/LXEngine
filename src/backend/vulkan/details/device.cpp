#include "device.hpp"
#include "descriptors/descriptor_manager.hpp"
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace LX_core {
namespace backend {

namespace {
static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {

  std::cerr << "[Vulkan Validation Layer]: " << pCallbackData->pMessage
            << std::endl;
  return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

void populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
}

bool checkValidationLayerSupport(
    const std::vector<const char *> &validationLayers) {
#ifdef NDEBUG
  return false;
#endif

  auto toCheck = validationLayers;
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  std::vector<const char *> foundLayers;
  for (const char *layerName : toCheck) {
    for (const auto &layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        foundLayers.push_back(layerName);
        break;
      }
    }
  }

  if (foundLayers.size() != toCheck.size()) {
    std::cerr << "Validation layers not supported: " << toCheck.size()
              << " layers" << std::endl;
    for (const char *layerName : toCheck) {
      std::cerr << "  " << layerName << std::endl;
    }
    return false;
  }

  return true;
}

VkSurfaceFormatKHR findBestSurfaceFormat(VkPhysicalDevice physicalDevice,
                                         VkSurfaceKHR surface) {
  // 1. 获取硬件支持的所有表面格式
  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                       nullptr);

  if (formatCount == 0) {
    throw std::runtime_error("No surface formats found!");
  }

  std::vector<VkSurfaceFormatKHR> availableFormats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                       availableFormats.data());

  // 2. 筛选最优格式
  for (const auto &availableFormat : availableFormats) {
    // 我们优先寻找 B8G8R8A8 或 R8G8B8A8 的 SRGB 非线性版本
    // SRGB 可以提供更准确的视觉亮度（Gamma 校正）
    for (const auto &availableFormat : availableFormats) {
      if ((availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB ||
           availableFormat.format == VK_FORMAT_R8G8B8A8_SRGB) &&
          availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return availableFormat;
      }
    }
  }

  // 3. 兜底方案：如果找不到 SRGB，直接返回第一个支持的格式
  return availableFormats[0];
}

} // anonymous namespace

VkFormat
VulkanDevice::findSupportedFormat(const std::vector<VkFormat> &candidates,
                                  VkImageTiling tiling,
                                  VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features)
      return format;
    else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
             (props.optimalTilingFeatures & features) == features)
      return format;
  }
  throw std::runtime_error("failed to find supported format!");
}

VkImageAspectFlags VulkanDevice::getDepthAspectMask() const {
  switch (m_depthFormat) {
  case VK_FORMAT_D32_SFLOAT:
    return VK_IMAGE_ASPECT_DEPTH_BIT;

  case VK_FORMAT_D32_SFLOAT_S8_UINT:
  case VK_FORMAT_D24_UNORM_S8_UINT:
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

  default:
    throw std::runtime_error("Unsupported depth format!");
  }
}

VulkanDevice::VulkanDevice(Token) {}

VulkanDevice::~VulkanDevice() {
  if (m_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_device);
  }
  shutdown();
}

void VulkanDevice::createSurface() {
  m_surface = (VkSurfaceKHR)m_window->createGraphicsHandle(GraphicsAPI::Vulkan,
                                                           m_instance);
  if (m_surface == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to create Vulkan surface handle");
  }
}

void VulkanDevice::findSurfaceDepthFormat() {
  m_surfaceFormat = findBestSurfaceFormat(m_physicalDevice, m_surface);
  m_depthFormat = findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT}, // 具体创建深度纹理图的时候，需要根据选择的格式来设置
                                     // aspectMask
      VK_IMAGE_TILING_OPTIMAL, // 这种格式不要直接用cpp指针访问内存，因为是硬件理解的格式。
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT // 进一步要求这个格式支持深度附件功能
  );
}

void VulkanDevice::initialize(WindowSharedPtr window, const char *appName,
                              uint32_t appVersion, const char *engineName,
                              uint32_t engineVersion, uint32_t apiVersion,
                              std::vector<const char *> validationLayers) {
  m_window = window;
  m_validationLayers = validationLayers;
  m_instanceExtensions = {};
  m_extent = {static_cast<uint32_t>(window->getWidth()),
              static_cast<uint32_t>(window->getHeight())};
  m_window->getRequiredExtensions(m_instanceExtensions);

  createInstance(appName, appVersion, engineName, engineVersion, apiVersion);
  createSurface();
  m_deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      // VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
      // VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
  };
  pickPhysicalDevice();
  findSurfaceDepthFormat();
  createLogicalDevice();
}

void VulkanDevice::shutdown() {
  // Destroy the descriptor manager (and all descriptor pools/layouts) before
  // destroying the VkDevice. Otherwise the descriptor manager destructor will
  // see VK_NULL_HANDLE device handles and validation will complain.
  if (m_descriptorManager) {
    m_descriptorManager.reset();
  }

  if (m_device != VK_NULL_HANDLE) {
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
  }
  if (m_surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_window->destroyGraphicsHandle(GraphicsAPI::Vulkan, getInstance(),
                                    m_surface);
    m_window = nullptr;
    m_surface = VK_NULL_HANDLE;
    m_surfaceFormat = {};
    m_depthFormat = VK_FORMAT_UNDEFINED;
  }

  if (m_debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    m_debugMessenger = VK_NULL_HANDLE;
  }

  if (m_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }
}

void VulkanDevice::createInstance(const char *appName, uint32_t appVersion,
                                  const char *engineName,
                                  uint32_t engineVersion, uint32_t apiVersion) {
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = appName;
  appInfo.applicationVersion = appVersion;
  appInfo.pEngineName = engineName;
  appInfo.engineVersion = engineVersion;
  appInfo.apiVersion = apiVersion;

  // Create info
  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  // Check for validation layers (optional)
  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  bool supported = checkValidationLayerSupport(m_validationLayers);
  if (supported) {
    m_instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    createInfo.ppEnabledLayerNames = m_validationLayers.data();
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(m_validationLayers.size());

    // 让 Instance 在创建和销毁时也能输出调试信息
    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;
  }

  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(m_instanceExtensions.size());
  createInfo.ppEnabledExtensionNames = m_instanceExtensions.data();

  if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan instance!");
  }

  if (supported) {
    if (CreateDebugUtilsMessengerEXT(m_instance, &debugCreateInfo, nullptr,
                                     &m_debugMessenger) != VK_SUCCESS) {
      std::cerr << "Failed to set up debug messenger!" << std::endl;
    }
  }
}

VulkanDevice::QueueFamilyIndices
VulkanDevice::findQueueFamilies(VkPhysicalDevice device) {

  // 1. 获取该显卡支持的所有队列族数量
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  // 2. 获取具体的队列族属性
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  int i = 0;
  for (const auto &queueFamily : queueFamilies) {
    // 检查是否支持图形渲染 (Graphics)
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      m_queueIndices.graphicsFamily = i;
    }

    // 检查是否支持显示 (Present)
    // 注意：这需要 Surface 参与，因为有的显卡能画图但不能接显示器
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
    if (presentSupport) {
      m_queueIndices.presentFamily = i;
    }

    if (m_queueIndices.isComplete()) {
      break; // 找齐了就撤
    }
    i++;
  }

  return m_queueIndices;
}

bool VulkanDevice::checkDeviceExtensionSupport(
    VkPhysicalDevice device, std::vector<const char *> extensionsRequired) {
  // 1. 获取该物理设备支持的所有扩展数量
  uint32_t extensionCount;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       nullptr);

  // 2. 获取所有扩展的具体属性
  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       availableExtensions.data());

  // 3. 使用 set 存储所有已找到的扩展名称，方便查找
  std::set<std::string> requiredExtensions(extensionsRequired.begin(),
                                           extensionsRequired.end());

  // 4. 遍历设备支持的扩展，从“待选名单”中剔除
  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  // 5. 如果集合为空，说明所有要求的扩展都在设备支持列表中
  return requiredExtensions.empty();
}

bool VulkanDevice::isDeviceSuitable(
    VkPhysicalDevice device, std::vector<const char *> extensionsRequired) {
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(device, &properties);

  // 1. 检查队列族（是否有图形队列）
  QueueFamilyIndices queueIndices = findQueueFamilies(device);

  // 2. 检查扩展（是否支持交换链）
  bool extensionsSupported =
      checkDeviceExtensionSupport(device, extensionsRequired);

  // 3. 只有功能完备，且是独显，才是最优选
  return queueIndices.isComplete() && extensionsSupported &&
         (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
}

void VulkanDevice::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    throw std::runtime_error("Failed to find GPUs with Vulkan support!");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

  // Find the best discrete GPU, or fall back to the first available
  for (const auto &device : devices) {
    // 只有当显卡【功能完备】时，才考虑它
    if (isDeviceSuitable(device, m_deviceExtensions)) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(device, &props);

      m_physicalDevice = device;

      // 如果是独显，那是完美选择，直接退出循环
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        std::cout << "Selected discrete GPU: " << props.deviceName << "\n";
        break;
      }
      // 如果是集成显卡，先记下来，继续看看后面有没有独显
    }
  }

  if (m_physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to find a suitable GPU!");
  }

  m_queueIndices = findQueueFamilies(m_physicalDevice);
  if (!m_queueIndices.isComplete()) {
    throw std::runtime_error("Failed to find required queue families!");
  }
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
  std::cout << "  Driver version: " << properties.driverVersion << std::endl;
  std::cout << "  Vulkan API: " << VK_VERSION_MAJOR(properties.apiVersion)
            << "." << VK_VERSION_MINOR(properties.apiVersion) << "."
            << VK_VERSION_PATCH(properties.apiVersion) << std::endl;
}

void VulkanDevice::createLogicalDevice() {
  // Get queue family properties
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount,
                                           queueFamilies.data());

  if (!m_queueIndices.isComplete()) {
    throw std::runtime_error("Failed to find required queue families!");
  }

  // Use a set to ensure unique queue families
  std::set<uint32_t> uniqueQueueFamilies = {
      getGraphicsQueueFamilyIndex(),
      getPresentQueueFamilyIndex(),
  };

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  float queuePriority = 1.0f;

  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  // Device features (currently empty but required for future extensions)
  VkPhysicalDeviceFeatures deviceFeatures{};

  // Create logical device
  VkDeviceCreateInfo deviceCreateInfo{};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
  deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

  deviceCreateInfo.enabledExtensionCount =
      static_cast<uint32_t>(m_deviceExtensions.size());
  deviceCreateInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

#ifdef NDEBUG
  deviceCreateInfo.enabledLayerCount = 0;
#else
  deviceCreateInfo.enabledLayerCount =
      static_cast<uint32_t>(m_validationLayers.size());
  deviceCreateInfo.ppEnabledLayerNames = m_validationLayers.data();
#endif

  if (vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create logical device!");
  }

  // Get queue handles
  vkGetDeviceQueue(m_device, getGraphicsQueueFamilyIndex(), 0,
                   &m_graphicsQueue);
  vkGetDeviceQueue(m_device, getPresentQueueFamilyIndex(), 0, &m_presentQueue);

  // Create descriptor manager with device reference
  m_descriptorManager = VulkanDescriptorManager::create(*this);
}

uint32_t
VulkanDevice::findMemoryTypeIndex(uint32_t typeFilter,
                                  VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  std::cerr << "Failed to find suitable memory type!" << typeFilter << " "
            << properties << std::endl;

  throw std::runtime_error("Failed to find suitable memory type!");
}

} // namespace backend
} // namespace LX_core
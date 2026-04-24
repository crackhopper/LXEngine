# renderer-backend-vulkan Specification

## Purpose
TBD - created by archiving change implement-renderer-framework. Update Purpose after archive.

## MODIFIED Requirements

### Requirement: VulkanDevice shall initialize Vulkan instance and enumerate physical devices

The VulkanDevice SHALL initialize the Vulkan subsystem with:
- VK_INSTANCE with required global extensions (Vulkan surface for window integration)
- Enumerate physical devices and select the first discrete GPU if available, otherwise the first available device
- Log device properties (name, type, driver version) for debugging
- Factory pattern: VulkanDevice objects MUST be created via `VulkanDevice::create()` with Token
- Initialization requires `WindowSharedPtr` and application name parameters

#### Scenario: Device initialization succeeds with valid GPU
- **WHEN** VulkanDriver is installed and system has a discrete GPU
- **WHEN** `VulkanDevice::create()` is called and `initialize(window, "AppName")` is invoked
- **THEN** `m_physicalDevice` SHALL be valid and Vulkan instance created successfully
- **THEN** Graphics and present queues SHALL be available
- **THEN** `getInstance()` SHALL return valid VkInstance

#### Scenario: Device initialization fails gracefully on no GPU
- **WHEN** System has no Vulkan-capable GPU
- **THEN** `initialize()` SHALL throw `std::runtime_error` with appropriate error message
- **AND** No Vulkan resources SHALL be leaked

### Requirement: VulkanDevice shall create logical device with graphics and present queues

The VulkanDevice SHALL create a logical device with:
- Graphics queue family supporting rendering commands
- Present queue family supporting window presentation
- Device extensions required for swapchain (VK_KHR_SWAPCHAIN_EXTENSION_NAME)
- VulkanDescriptorManager initialized for efficient GPU resource management

#### Scenario: Logical device creation with graphics queue
- **WHEN** Physical device supports graphics operations
- **WHEN** `initialize(window, "AppName")` is invoked
- **THEN** `m_device` SHALL be valid VkDevice
- **AND** `m_graphicsQueue` SHALL be valid with at least one queue
- **AND** `getDescriptorManager()` SHALL return reference to initialized manager

## REMOVED Requirements

### Requirement: VulkanDevice::initialize() shall return boolean success indicator

**Reason**: Implementation changed to exception-based error handling. Boolean returns were removed in favor of `std::runtime_error` exceptions.

**Migration**: Wrap initialization in try/catch block. Catch `std::exception` for error handling.

### Requirement: VulkanDevice shall have public constructor

**Reason**: Factory pattern enforcement via private Token struct.

**Migration**: Use `VulkanDevice::create()` factory method instead of direct construction.

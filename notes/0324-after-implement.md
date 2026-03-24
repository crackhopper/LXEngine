# `void VulkanDevice::createInstance()`
问题： glfwExtensions 这种，引入了图形后端的概念。实际我们应该在窗口系统中。

代码：
```cpp
  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions;
```

infra/window/window.hpp 中，添加一些静态函数，直接返回窗口系统所需要的 vulkan 扩展。而createInstance应该通过一个入参来获取到窗口系统所需要的 vulkan 扩展。从而完成窗口系统和vulkan后端的解耦合。
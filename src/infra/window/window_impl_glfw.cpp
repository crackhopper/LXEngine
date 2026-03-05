#ifdef USE_GLFW
#include "window.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

#include "window.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace LX_infra {

struct Window::Impl {
    int width;
    int height;
    const char* title;
    GLFWwindow* window = nullptr;

    Impl(int w, int h, const char* t) : width(w), height(h), title(t) {
        if (!glfwInit()) throw std::runtime_error("GLFW init failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window) throw std::runtime_error("GLFW create window failed");
    }

    ~Impl() {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }

    bool shouldClose() const {
        return glfwWindowShouldClose(window);
    }

    VkSurfaceKHR getVulkanSurface(VkInstance instance) const {
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan surface");
        return surface;
    }
};

Window::Window(int width, int height, const char* title)
    : pImpl(new Impl(width, height, title)) {}

Window::~Window() { delete pImpl; }
int Window::getWidth() const { return pImpl->width; }
int Window::getHeight() const { return pImpl->height; }
bool Window::shouldClose() const { return pImpl->shouldClose(); }
VkSurfaceKHR Window::getVulkanSurface(VkInstance instance) const {
    return pImpl->getVulkanSurface(instance);
}

} // namespace LX_infra
#endif
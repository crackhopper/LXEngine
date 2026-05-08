// REQ-DIAG-MIN: minimal Vulkan demo isolating the resize-triggered
// black-screen / flicker bug. Reuses the suspect path (Window, VulkanDevice,
// VulkanSwapchain, VulkanRenderPass, VulkanFrameBuffer) but bypasses every
// layer that scene_viewer uses on top of it: no VulkanResourceManager, no
// VulkanCommandBufferManager, no VulkanDescriptorManager, no FrameGraph, no
// ImGui, no per-draw push constants, no UBOs, no textures, no scene tree.
//
// What it draws: two NDC-space quads at z = 0.3 (green) and z = 0.7 (red),
// with depth test enabled (LESS). A correctly working depth attachment will
// always show the green quad on top of the red one. If the green disappears,
// turns red, or flickers between green and red after a maximize / restore /
// monitor switch, the bug is reproducible at this layer.
//
// All Vulkan calls beyond the four reused classes are issued directly here,
// so any deviation from working behaviour identifies the offending object.

#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/render_objects/swapchain.hpp"
#include "core/platform/types.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr u32 kMaxFramesInFlight = 1;

struct Vertex {
  float pos[3];
  float color[3];
};

constexpr Vertex kVertices[] = {
    // Front quad (green, z = 0.3) — should occlude the red one
    {{-0.5f, -0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, -0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.3f}, {0.0f, 1.0f, 0.0f}},
    // Back quad (red, z = 0.7) — wider so partial occlusion is obvious
    {{-0.7f, -0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
    {{0.7f, -0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
    {{0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
    {{-0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}},
};

constexpr u32 kIndices[] = {
    0, 1, 2, 2, 3, 0, // front quad
    4, 5, 6, 6, 7, 4, // back quad
};

std::vector<u32> readShaderBytes(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader file: " + path.string());
  }
  const auto size = static_cast<usize>(file.tellg());
  if (size % sizeof(u32) != 0) {
    throw std::runtime_error("Shader bytecode size not 4-byte aligned: " +
                             path.string());
  }
  std::vector<u32> code(size / sizeof(u32));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(code.data()),
            static_cast<std::streamsize>(size));
  return code;
}

VkShaderModule createShaderModule(VkDevice device,
                                  const std::vector<u32> &code) {
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size() * sizeof(u32);
  info.pCode = code.data();
  VkShaderModule shaderModule = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create shader module");
  }
  return shaderModule;
}

struct HostBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;

  void destroy() {
    if (buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, buffer, nullptr);
      buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
      vkFreeMemory(device, memory, nullptr);
      memory = VK_NULL_HANDLE;
    }
  }
};

HostBuffer createHostBuffer(LX_core::backend::VulkanDevice &device,
                            VkDeviceSize size, VkBufferUsageFlags usage,
                            const void *data) {
  HostBuffer out;
  out.device = device.getLogicalDevice();

  VkBufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(out.device, &info, nullptr, &out.buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create host buffer");
  }

  VkMemoryRequirements memReq{};
  vkGetBufferMemoryRequirements(out.device, out.buffer, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = device.findMemoryTypeIndex(
      memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (vkAllocateMemory(out.device, &allocInfo, nullptr, &out.memory) !=
      VK_SUCCESS) {
    vkDestroyBuffer(out.device, out.buffer, nullptr);
    throw std::runtime_error("Failed to allocate host buffer memory");
  }
  vkBindBufferMemory(out.device, out.buffer, out.memory, 0);

  void *mapped = nullptr;
  vkMapMemory(out.device, out.memory, 0, size, 0, &mapped);
  std::memcpy(mapped, data, static_cast<usize>(size));
  vkUnmapMemory(out.device, out.memory);
  return out;
}

VkPipeline createMinimalPipeline(VkDevice device, VkPipelineLayout layout,
                                 VkRenderPass renderPass,
                                 VkShaderModule vertModule,
                                 VkShaderModule fragModule) {
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertModule;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragModule;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> attrs{};
  attrs[0].binding = 0;
  attrs[0].location = 0;
  attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[0].offset = offsetof(Vertex, pos);
  attrs[1].binding = 0;
  attrs[1].location = 1;
  attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attrs[1].offset = offsetof(Vertex, color);

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &binding;
  vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(attrs.size());
  vertexInput.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorAttachment{};
  colorAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1;
  colorBlend.pAttachments = &colorAttachment;

  std::array<VkDynamicState, 2> dynamics = {VK_DYNAMIC_STATE_VIEWPORT,
                                            VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<u32>(dynamics.size());
  dynamicState.pDynamicStates = dynamics.data();

  VkGraphicsPipelineCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.stageCount = static_cast<u32>(stages.size());
  info.pStages = stages.data();
  info.pVertexInputState = &vertexInput;
  info.pInputAssemblyState = &inputAssembly;
  info.pViewportState = &viewportState;
  info.pRasterizationState = &rasterizer;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depthStencil;
  info.pColorBlendState = &colorBlend;
  info.pDynamicState = &dynamicState;
  info.layout = layout;
  info.renderPass = renderPass;
  info.subpass = 0;

  VkPipeline pipeline = VK_NULL_HANDLE;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr,
                                &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create graphics pipeline");
  }
  return pipeline;
}

void recordFrame(VkCommandBuffer cmd,
                 LX_core::backend::VulkanRenderPass &renderPass,
                 LX_core::backend::VulkanFrameBuffer &framebuffer,
                 VkExtent2D extent, VkPipeline pipeline,
                 VkBuffer vertexBuffer, VkBuffer indexBuffer,
                 u32 indexCount) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkRenderPassBeginInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpInfo.renderPass = renderPass.getHandle();
  rpInfo.framebuffer = framebuffer.getHandle();
  rpInfo.renderArea.offset = {0, 0};
  rpInfo.renderArea.extent = extent;
  const auto &clearValues = renderPass.getClearValues();
  rpInfo.clearValueCount = static_cast<u32>(clearValues.size());
  rpInfo.pClearValues = clearValues.data();
  vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = extent;
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
  vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);
}

} // namespace

int main() {
  using namespace LX_core::backend;

  expSetEnvVK();
  if (!initializeRuntimeAssetRoot()) {
    std::cerr << "[minimal_resize] failed to initialize runtime asset root\n";
    return 1;
  }

  try {
    LX_infra::Window::Initialize();
    auto window = std::make_shared<LX_infra::Window>(
        "demo_minimal_resize", kWindowWidth, kWindowHeight);

    auto device = VulkanDevice::create();
    device->initialize(window, "demo_minimal_resize");

    auto renderPass = VulkanRenderPass::create(
        *device, device->getSurfaceFormat().format, device->getDepthFormat());

    auto swapchain =
        VulkanSwapchain::create(*device, window, kMaxFramesInFlight);
    swapchain->initialize(*renderPass);

    VkDevice logicalDevice = device->getLogicalDevice();
    VkQueue graphicsQueue = device->getGraphicsQueue();

    // === Shader modules ===
    const auto shaderDir = getRuntimeShaderBinaryDir();
    auto vertCode = readShaderBytes(shaderDir / "minimal.vert.spv");
    auto fragCode = readShaderBytes(shaderDir / "minimal.frag.spv");
    VkShaderModule vertModule = createShaderModule(logicalDevice, vertCode);
    VkShaderModule fragModule = createShaderModule(logicalDevice, fragCode);

    // === Pipeline layout (no descriptor sets, no push constants) ===
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(logicalDevice, &layoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create pipeline layout");
    }

    VkPipeline pipeline =
        createMinimalPipeline(logicalDevice, pipelineLayout,
                              renderPass->getHandle(), vertModule, fragModule);

    // === Vertex / Index buffers ===
    HostBuffer vertexBuffer =
        createHostBuffer(*device, sizeof(kVertices),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kVertices);
    HostBuffer indexBuffer = createHostBuffer(
        *device, sizeof(kIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kIndices);

    // === Command pool / buffer ===
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device->getGraphicsQueueFamilyIndex();
    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create command pool");
    }

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(logicalDevice, &cmdAlloc, &cmd) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate command buffer");
    }

    if (expRendererDebugEnabled()) {
      const VkExtent2D extent = swapchain->getExtent();
      std::cerr << "[MinimalResizeDebug] start: extent=" << extent.width << "x"
                << extent.height
                << " imageCount=" << swapchain->getImageCount()
                << " maxFramesInFlight=" << kMaxFramesInFlight << std::endl;
    }

    // === Main loop ===
    u32 frameIndex = 0;
    constexpr u32 kIndexCount = sizeof(kIndices) / sizeof(u32);

    while (!window->shouldClose()) {
      if (window->getWidth() <= 0 || window->getHeight() <= 0) {
        continue;
      }

      const u32 currentFrameIndex = frameIndex % kMaxFramesInFlight;
      u32 imageIndex = 0;

      VkResult acquireResult =
          swapchain->acquireNextImage(currentFrameIndex, imageIndex);
      if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
          acquireResult == VK_SUBOPTIMAL_KHR) {
        if (expRendererDebugEnabled()) {
          std::cerr << "[MinimalResizeDebug] acquire returned "
                    << (acquireResult == VK_ERROR_OUT_OF_DATE_KHR
                            ? "VK_ERROR_OUT_OF_DATE_KHR"
                            : "VK_SUBOPTIMAL_KHR")
                    << ", rebuilding" << std::endl;
        }
        swapchain->waitIdle();
        swapchain->rebuild(*renderPass);
        continue;
      }
      if (acquireResult != VK_SUCCESS) {
        if (expRendererDebugEnabled()) {
          std::cerr << "[MinimalResizeDebug] acquire failed VkResult="
                    << static_cast<int>(acquireResult) << std::endl;
        }
        continue;
      }

      const VkExtent2D extent = swapchain->getExtent();

      vkResetCommandBuffer(cmd, 0);
      recordFrame(cmd, *renderPass, swapchain->getFramebuffer(imageIndex),
                  extent, pipeline, vertexBuffer.buffer, indexBuffer.buffer,
                  kIndexCount);

      VkSemaphore waitSem =
          swapchain->getImageAvailableSemaphore(currentFrameIndex);
      VkSemaphore signalSem =
          swapchain->getRenderFinishedSemaphore(currentFrameIndex);
      VkPipelineStageFlags waitStage =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

      VkSubmitInfo submitInfo{};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.waitSemaphoreCount = 1;
      submitInfo.pWaitSemaphores = &waitSem;
      submitInfo.pWaitDstStageMask = &waitStage;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &cmd;
      submitInfo.signalSemaphoreCount = 1;
      submitInfo.pSignalSemaphores = &signalSem;

      VkFence fence = swapchain->getInFlightFence(currentFrameIndex);
      vkResetFences(logicalDevice, 1, &fence);
      if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        std::cerr << "[minimal_resize] vkQueueSubmit failed\n";
        continue;
      }

      VkResult presentResult =
          swapchain->present(currentFrameIndex, imageIndex);
      if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
          presentResult == VK_SUBOPTIMAL_KHR) {
        if (expRendererDebugEnabled()) {
          std::cerr << "[MinimalResizeDebug] present returned "
                    << (presentResult == VK_ERROR_OUT_OF_DATE_KHR
                            ? "VK_ERROR_OUT_OF_DATE_KHR"
                            : "VK_SUBOPTIMAL_KHR")
                    << ", rebuilding" << std::endl;
        }
        swapchain->waitIdle();
        swapchain->rebuild(*renderPass);
        ++frameIndex;
        continue;
      }
      if (presentResult != VK_SUCCESS && expRendererDebugEnabled()) {
        std::cerr << "[MinimalResizeDebug] present failed VkResult="
                  << static_cast<int>(presentResult) << std::endl;
      }

      ++frameIndex;
    }

    vkDeviceWaitIdle(logicalDevice);

    // === Cleanup ===
    vertexBuffer.destroy();
    indexBuffer.destroy();
    vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
    vkDestroyPipeline(logicalDevice, pipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);
    vkDestroyShaderModule(logicalDevice, vertModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragModule, nullptr);

    swapchain.reset();
    renderPass.reset();
    device.reset();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[minimal_resize] fatal: " << e.what() << "\n";
    return 2;
  }
}

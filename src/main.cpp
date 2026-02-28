#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/glm.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <unordered_map>

// 为了计算 glm 数据结构的hash，需要引入
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobjloader/tiny_obj_loader.h>

#include "utils.h"

const bool GLOBAL_CONTROL_RENDER_BLACK = false;

const bool GLOBAL_CONTROL_ROTATE = false;
const bool GLOBAL_CONTROL_MIPMAP = true;
const bool GLOBAL_CONTROL_USE_MIPLODBIAS = false;
const int GLOBAL_CONTROL_MIPLODBIAS = 0;

const bool GLOBAL_CONTROL_MSAA = true;
const bool GLOBAL_CONTROL_MSAA_SAMPLE_RATE_SHADING = false;
const float GLOBAL_CONTROL_MSAA_MIN_SAMPLE_SHADING = 0.2f;

const std::string MODEL_PATH = "models/viking_room.obj";
const std::string TEXTURE_PATH = "textures/viking_room.png";


#include "graphics/gpu_device.hpp"
#include "graphics/resources/vertex.hpp"
#include "graphics/resources/ubo.hpp"

using Vertex = LX::Vertex;


class HelloTriangleApplication {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  SDL_Window *window;
  const uint32_t WIDTH = 800;
  const uint32_t HEIGHT = 600;

  int MAX_FRAMES_IN_FLIGHT = 2;

  const std::vector<const char *> validationLayers = {
      "VK_LAYER_KHRONOS_validation"};

  const std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

#ifdef USE_GEOMETRY_SHADER
  const bool useGeometryShader = true;
#else
  const bool useGeometryShader = false;
#endif

  VkInstance instance;

  static std::vector<char> readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
      throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
  }

  VkRenderPass renderPass;
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;

  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkCommandPool commandPool;
  std::vector<VkCommandBuffer> commandBuffers;

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    /*************** 指令录制开始 */
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("failed to begin recording command buffer!");
    }
    /***************** begin render pass */
    // 填充 render pass 启动配置
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    // 绑定帧
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    // 设定渲染区域
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;
    // 设定clear value
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();

    // 启动render pass
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    /*************** 开始绘制 */
    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphicsPipeline);

    // 设定动态变量
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainExtent.width);
    viewport.height = static_cast<float>(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // 绑定描述符集
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSets[imageIndex],
                            0, nullptr);

    if (!GLOBAL_CONTROL_RENDER_BLACK) {
      vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1,
                       0, 0, 0);
    }

    // 渲染 ImGui
    std::lock_guard<std::mutex> lock(drawDataMutex);
    if (drawDataForRenderThread) {
      ImGui_ImplVulkan_RenderDrawData(drawDataForRenderThread, commandBuffer);
    }

    // 结束render pass
    vkCmdEndRenderPass(commandBuffer);
    /*************** 绘制结束 */

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to record command buffer!");
    }
    /*************** 指令录制结束 */
  }

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1 << i)) &&
          (memProperties.memoryTypes[i].propertyFlags & properties) ==
              properties) {
        return i;
      }
    }

    throw std::runtime_error("failed to find suitable memory type!");
  }

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices; // 注意更改到了 uint32_t

  void loadModel() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                          MODEL_PATH.c_str())) {
      throw std::runtime_error(err);
    }

    // OBJ 文件，包含： 位置(positions)、法项(normals)、纹理坐标(texture coords)
    // 保存在 `attrib.vertices` , `attrib.normals` 和 `attrib.texcoords`
    // (整体的数据)
    //
    // `shapes` 包含了所有对象以及它们的面(face)的索引信息。
    // 每个面包含了一组顶点，每个顶点包含了对应的 position, normal 和 texture
    // coords 信息。 OBJ文件还可以对每个face定义对应的material 和
    // texture。我们暂时i忽略这些。

    // 这里面我们用一个 unorder_map (hashmap)
    // ，以顶点作为key，来确保顶点数据唯一性。
    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    // 我们接下来要把所有的face拼接到一个顶点数据中。
    for (const auto &shape : shapes) {
      for (const auto &index : shape.mesh.indices) {
        Vertex vertex{};
        // 由于 attrib.vertices 中是展开保存的数据，所以我们要用 3*idx+i
        // 的方式取数值
        vertex.pos = {attrib.vertices[3 * index.vertex_index + 0],
                      attrib.vertices[3 * index.vertex_index + 1],
                      attrib.vertices[3 * index.vertex_index + 2]};
        // 纹理坐标类似
        // vertex.texCoord = {attrib.texcoords[2 * index.texcoord_index + 0],
        //                    attrib.texcoords[2 * index.texcoord_index + 1]};
        vertex.texCoord = {
            attrib.texcoords[2 * index.texcoord_index + 0],
            // vulkan的纹理坐标y方向和OBJ文件定义的Y方向并不一样。因此需要反转y坐标
            1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};

        vertex.color = {1.0f, 1.0f, 1.0f};

        if (uniqueVertices.count(vertex) == 0) {
          uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
          vertices.push_back(vertex);
        }
        indices.push_back(uniqueVertices[vertex]);
      }
    }
  }

  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;
  VkBuffer indexBuffer;
  VkDeviceMemory indexBufferMemory;

  std::vector<VkBuffer> uniformBuffers;
  std::vector<VkDeviceMemory> uniformBuffersMemory;
  std::vector<void *> uniformBuffersMapped;

  VkDescriptorPool descriptorPool;
  std::vector<VkDescriptorSet> descriptorSets;

  void createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                               descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = uniformBuffers[i];
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(UniformBufferObject);

      descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[0].dstSet = descriptorSets[i];
      descriptorWrites[0].dstBinding = 0;
      descriptorWrites[0].dstArrayElement = 0;
      descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[0].descriptorCount = 1;
      descriptorWrites[0].pBufferInfo = &bufferInfo;

      VkDescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.imageView = textureImageView;
      imageInfo.sampler = textureSampler;

      descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[1].dstSet = descriptorSets[i];
      descriptorWrites[1].dstBinding = 1;
      descriptorWrites[1].dstArrayElement = 0;
      descriptorWrites[1].descriptorType =
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptorWrites[1].descriptorCount = 1;
      descriptorWrites[1].pImageInfo = &imageInfo;

      vkUpdateDescriptorSets(device,
                             static_cast<uint32_t>(descriptorWrites.size()),
                             descriptorWrites.data(), 0, nullptr);
    }
  }

  void createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount =
        static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 2);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount =
        static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 4);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT：允许单独释放
    // descriptor set，建议添加。
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.pPoolSizes = poolSizes.data();
    // maxSets ≥ 每帧渲染使用的 descriptor set 数量（一般 ≥ MAX_FRAMES_IN_FLIGHT
    // * 2，安全起见多一些）。
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor pool!");
    }
  }

  void createUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   uniformBuffers[i], uniformBuffersMemory[i]);
      vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0,
                  &uniformBuffersMapped[i]);
    }
  }

  void createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
  }

  void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
  }
  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0; // Optional
    copyRegion.dstOffset = 0; // Optional
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
  }

  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkBuffer &buffer,
                    VkDeviceMemory &bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0); // 0代表偏移量
  }

  VkImage colorImage;
  VkDeviceMemory colorImageMemory;
  VkImageView colorImageView;

  void createColorResources() {
    VkFormat colorFormat = swapChainImageFormat;

    createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples,
                colorFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage,
                colorImageMemory);
    colorImageView =
        createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }

  VkImage depthImage;
  VkDeviceMemory depthImageMemory;
  VkImageView depthImageView;

  void createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples,
                depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage,
                depthImageMemory);

    depthImageView =
        createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    // 手动显式转化layout （实际上并不需要，renderPass会自动搞定，这里只是演示）
    transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
  }

  // 判断选择的格式是否支持stencil(模板)
  bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT;
  }

  // 我们具体查找格式的用法
  VkFormat findDepthFormat() {
    return findSupportedFormat({VK_FORMAT_D32_SFLOAT,
                                VK_FORMAT_D32_SFLOAT_S8_UINT,
                                VK_FORMAT_D24_UNORM_S8_UINT},
                               VK_IMAGE_TILING_OPTIMAL,
                               VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  }

  VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates,
                               VkImageTiling tiling,
                               VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
      VkFormatProperties props;
      vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
      if (tiling == VK_IMAGE_TILING_LINEAR &&
          // props.linearTilingFeatures 返回了格式所支持的 linear tiling 下的
          // feature
          (props.linearTilingFeatures & features) == features) {
        return format;
      } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                 // props.optimalTilingFeatures 返回了格式所支持的 optimal
                 // tiling 下的 feature
                 (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }
    // 查找格式失败，扔出异常。
    throw std::runtime_error("failed to find supported format!");
  }

  void transitionImageLayout(VkImage image, VkFormat format,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             uint32_t mipLevels) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // 定义一个图像内存屏障结构，用于描述访问依赖和布局转换
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    // 指定旧布局和新布局
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;

    // 如果图像需要在不同队列族之间转移所有权，用下面字段指定
    // 这里不需要队列转移，所以使用 VK_QUEUE_FAMILY_IGNORED
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    // 指定操作的图像
    barrier.image = image;
    // subresourceRange 指定影响的图像子资源（mipmap层和数组层）
    barrier.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;                   // 操作颜色数据
    barrier.subresourceRange.baseMipLevel = 0;       // 从第0层mipmap开始
    barrier.subresourceRange.levelCount = mipLevels; // 影响mipmap
    barrier.subresourceRange.baseArrayLayer = 0;     // 从第0层数组开始
    barrier.subresourceRange.layerCount = 1;         // 只影响1层数组

    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

      if (hasStencilComponent(format)) {
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
      }
    } else {
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    // 定义源和目标的 pipeline 阶段，用于同步
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    // 判断是哪种布局转换
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      // UNDEFINED -> TRANSFER_DST_OPTIMAL
      // 未定义布局，不需要等待任何操作
      barrier.srcAccessMask = 0;
      // 下一步操作是写入图像
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

      // 源阶段为 pipeline 开头 （stage不能为0，必须是有效阶段；所以才这么设置）
      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      // 目标阶段为 transfer 阶段
      destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
      // 确保 transfer 写入完成后，shader 才能读取
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      // 源阶段：写入阶段完成
      sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      // 目标阶段：在片段着色器读取之前
      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
      // 其他布局转换不支持
      throw std::invalid_argument("unsupported layout transition!");
    }

    // 当然，layout transition操作，需要都记录到一个
    // commandBuffer，barrier才会生效。
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(commandBuffer);
  }

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  void createTextureImage() {
    int texWidth, texHeight, texChannels;
    // 加载图像像素为一个指针
    // stbi_uc *pixels = stbi_load("textures/texture.jpg"
    // ,&texWidth,&texHeight,&texChannels, STBI_rgb_alpha);
    stbi_uc *pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight,
                                &texChannels, STBI_rgb_alpha);

    mipLevels = static_cast<uint32_t>(
                    std::floor(std::log2(std::max(texWidth, texHeight)))) +
                1;

    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
      throw std::runtime_error("failed to load texture image!");
    }

    // 创建vkBuffer
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);
    // 复制pixel到我们的buffer
    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // 释放掉我们用stb加载的image
    stbi_image_free(pixels);

    createImage(
        texWidth, texHeight, mipLevels, VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

    transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
    copyBufferToImage(stagingBuffer, textureImage,
                      static_cast<uint32_t>(texWidth),
                      static_cast<uint32_t>(texHeight));
    // 当然最后还要cleanup
    // transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
    //                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //                       mipLevels);

    generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight,
                    mipLevels);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth,
                       int32_t texHeight, uint32_t mipLevels) {
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat,
                                        &formatProperties);

    if (!(formatProperties.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      throw std::runtime_error(
          "textureimage format unsupport linear blitting!");
    }
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    // 复用上面的barrier，来做若干次转化

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
      // 注意循环从1开始
      barrier.subresourceRange.baseMipLevel = i - 1;
      // 上一级mip level计算结束后，其布局为
      // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      // 本次执行前，上一级mip level需要切换为
      // VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      // 上一级mip level计算结束的动作，VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      // 本次执行前，上一级mip level需要准备好VK_ACCESS_TRANSFER_READ_BIT
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

      // 记录这个barrier，让布局转化生效。
      vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &barrier);

      // 布局准备好了，我们可以执行 blit了
      // source level: i-1 , dest level: i
      VkImageBlit blit{};

      // 表示源区域的定义：
      // 区域覆盖的像素为 [offset0, offset1)。

      // 源区域的最小角（offset0）
      // 包含(x,y,z)的坐标，表示源区域的起始 texel（**inclusive**）
      // 这里设置为 (0,0,0) 表示从源 mip 的左上角（或原点）开始。
      blit.srcOffsets[0] = {0, 0, 0};
      // 源区域的最大角（offset1），表示源区域的结束坐标（**exclusive**）。
      // 这里表示 到 源 mip 的右下角结束。
      blit.srcOffsets[1] = {mipWidth, mipHeight, 1};

      // 指定源图像子资源的“面”(aspect)。对于彩色纹理通常是 COLOR_BIT。
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      // 源子资源使用的 mip 级别。这里是 i-1，说明源是上一级（更高分辨率）的 mip
      // 级别。
      blit.srcSubresource.mipLevel = i - 1;
      // 源的起始 array layer（针对 array textures 或 cubemaps），
      // 这里从 layer 0 开始。
      blit.srcSubresource.baseArrayLayer = 0;
      // 源使用的层数（从 baseArrayLayer 开始的连续层数）。这里是 1，表示只 blit
      // 单层。
      blit.srcSubresource.layerCount = 1;

      // 目标区域和一些参数定义，可以参见源区域定义来类比。
      blit.dstOffsets[0] = {0, 0, 0};
      blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                            mipHeight > 1 ? mipHeight / 2 : 1, 1};
      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = i;
      blit.dstSubresource.baseArrayLayer = 0;
      blit.dstSubresource.layerCount = 1;

      // 执行blit操作
      vkCmdBlitImage(commandBuffer,
                     // 源图像，源图像布局
                     image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     // 目标图像，目标图像布局
                     image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1,               // region count
                     &blit,           /// blit参数列表（对应 region count）
                     VK_FILTER_LINEAR // 使用的滤波器
      );
      // 注意，blit操作必须被提交到具备 graphics能力的队列。

      // blit结束后，再将layout转化为 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      // 向pipeline中插入barrier（指定具体的源阶段和目标阶段，然后插入barrier）
      // barrier中定义了等待的动作
      vkCmdPipelineBarrier(
          commandBuffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT,        // 源阶段阶段掩码（必须完成）
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // 目标阶段掩码（要进入）
          0,            // 队列内同步；如果有其他subpass依赖，这里要指定
          0, nullptr,   // 用于全局（buffer/image 通用）的内存依赖
          0, nullptr,   // 用于 buffer 的同步和 layout/ownership 管理
          1, &barrier); // 用于image同步。

      // 循环中不断缩小 mipWidth和mipHeight
      if (mipWidth > 1)
        mipWidth /= 2;
      if (mipHeight > 1)
        mipHeight /= 2;
    }
    // 最后1个level，还没有做布局转化，补上
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);

    endSingleTimeCommands(commandBuffer);
  }
  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                         uint32_t height) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
  }

  uint32_t mipLevels;
  VkImage textureImage;
  VkDeviceMemory textureImageMemory;

  void createImage(
      uint32_t width, uint32_t height, uint32_t mipLevels,
      VkSampleCountFlagBits numSamples,
      VkFormat format,                  // 像素格式，如 R8G8B8A8_SRGB
      VkImageTiling tiling,             // 内存布局 （决定硬件访问效率）
      VkImageUsageFlags usage,          // 用途
      VkMemoryPropertyFlags properties, // 内存属性 (如，HOST_VISIBLE之类的)
      VkImage &image,                   // 输出: 创建好的图像对象
      VkDeviceMemory &imageMemory       // 输出: 绑定的显存对象
  ) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    // 图像类型：二维纹理
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    // 图像深度，二维图像为1
    imageInfo.extent.depth = 1;
    // mipmap层数
    imageInfo.mipLevels = mipLevels;
    // 图像数组层数，单张图片为1
    imageInfo.arrayLayers = 1;

    imageInfo.format = format;
    imageInfo.tiling = tiling;
    // GPU无法直接使用初始数据，第一次写入会覆盖现有内容。
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    // 目标图像是否支持多采样
    imageInfo.samples = numSamples;
    // 独占队列访问（仅图形队列）
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
      throw std::runtime_error("failed to create image!");
    }

    // 查询图像内存需求
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    // 配置内存分配信息
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to allocate image memory!");
    }
    // 将分配好的显存绑定到图像对象上
    vkBindImageMemory(device, image, imageMemory, 0);
  }

  VkImageView textureImageView;
  VkSampler textureSampler;
  void createTextureImageView() {
    textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                                       VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
  }

  void createTextureSampler(bool useMipMap = true) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // 针对 oversampling (简记：纹理过小)
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    // 针对 undersampling (简记：纹理过大)
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    // 超出区域的采样方式
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    // 各向异性过滤
    samplerInfo.anisotropyEnable = VK_TRUE;
    // 需要查询设备获取最大各向异性采样的限制
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    // 最多使用的采样点的限制
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    // 如果gpu不支持，那么我们也要关闭这个功能
    // samplerInfo.anisotropyEnable = VK_FALSE;
    // samplerInfo.maxAnisotropy = 1.0f;

    // 仅在 address mode 是 clamp to border的时候有效。
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    // 使用sampler的坐标范围
    // - True : [0,texWidth) x [0, texHeight)
    // - False : [0,1) x [0,1)
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    // 采样结果用来先做 Compare OP，随后的结果用来做filtering
    // 常用来做 percentage closer filtering (PCF)
    // https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    // mipmap滤波配置
    if (useMipMap) {
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      samplerInfo.minLod = 0.0f; // Optional
      samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
      if (GLOBAL_CONTROL_USE_MIPLODBIAS) {
        samplerInfo.mipLodBias = GLOBAL_CONTROL_MIPLODBIAS;
      } else {
        samplerInfo.mipLodBias = 0.0f; // Optional
      }
    } else {
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      samplerInfo.mipLodBias = 0.0f;
      samplerInfo.minLod = 0.0f;
      samplerInfo.maxLod = 0.0f;
    }

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create texture sampler!");
    }
  }

  void createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);
    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  void createCommandBuffers() {
    commandBuffers.resize(swapChainImageViews.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to allocate command buffers!");
    }
  }

  void createCommandPool() {
    LX::QueryQueueFamilyResult queueFamilyIndices = findQueueFamilies(physicalDevice);

    // 创建图形指令的命令池
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create command pool!");
    }
  }

  void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());
    // We'll then iterate through the image views and create framebuffers from
    // them:
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
      std::array<VkImageView, 3> attachments = {
          colorImageView, // 这里是渲染目标
          depthImageView,
          // 这块会最后resolve上面的渲染结果
          swapChainImageViews[i]};

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount = attachments.size();
      framebufferInfo.pAttachments = attachments.data();
      framebufferInfo.width = swapChainExtent.width;
      framebufferInfo.height = swapChainExtent.height;
      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                              &swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
      }
    }
  }

  void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;

    colorAttachment.samples = msaaSamples;

    // The loadOp and storeOp determine what to do with the data in the
    // attachment before rendering and after rendering.
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // The loadOp and storeOp apply to color and depth data, and stencilLoadOp
    // / stencilStoreOp apply to stencil data. Our application won't do
    // anything with the stencil buffer, so the results of loading and storing
    // are irrelevant.
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    // Textures and framebuffers in Vulkan are represented by VkImage objects
    // with a certain pixel format, however the layout of the pixels in memory
    // can change based on what you're trying to do with an image.
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // VkAttachmentDescription 结构体，用于描述深度附件。
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = msaaSamples;
    // **加载操作 (Load Op):** 在渲染开始时，如何处理深度附件中的现有数据。
    // VK_ATTACHMENT_LOAD_OP_CLEAR 表示在渲染此 Render Pass
    // 时，附件中的所有像素将被清除为预设值 (通常为 1.0f，表示最远)。
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // **存储操作 (Store Op):** 在渲染结束时，如何处理深度附件中的数据。
    // VK_ATTACHMENT_STORE_OP_DONT_CARE
    // 表示渲染完成后，深度数据的内容不需要被保留或写回内存。
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 用来resolve MSAA的attachment，显示到屏幕。
    VkAttachmentDescription colorAttachmentResolve{};
    colorAttachmentResolve.format = swapChainImageFormat;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // subpass相关
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment =
        0; // 这个代表索引0，和renderPassInfo.pAttachments[0]对应
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // 方便subpass引用附件。
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentResolveRef{};
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    // The index of the attachment in this array is directly referenced from
    // the fragment shader with the layout(location = 0) out vec4 outColor
    // directive!

    // 配置 subpass 的依赖。这里可以更好的配置，在渲染开始前，等待 image layout
    // transitions 完成。
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 3> attachments = {
        colorAttachment, depthAttachment, colorAttachmentResolve};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachments.size();
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create render pass!");
    }
  }

  VkDescriptorSetLayout descriptorSetLayout;
  void createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
        uboLayoutBinding, samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &descriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor set layout!");
    }
  }

  void createGraphicsPipeline() {
    // printCurrentDirectory_cpp17();
    // 读取编译好的 顶点着色器和片段着色器的 SPIR-V 代码
    auto vertShaderCode = readFile("./build/shaders/shader.vert.spv");
    auto fragShaderCode = readFile("./build/shaders/shader.frag.spv");

    // 创建顶点着色器模块和片段着色器模块
    auto vertShaderModule = createShaderModule(vertShaderCode);
    auto fragShaderModule = createShaderModule(fragShaderCode);

    // 填充 创建 pipeline 需要的 shader信息
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                      fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 定义 pipeline 输入装配阶段的状态
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 动态参数的具体设置： Viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // 动态参数的具体设置： Scissor
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;

    // 上面的设置绑定到 viewport state的设置中
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.pScissors = &scissor;

    // 创建动态参数的配置信息
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // rasterizer配置
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    // Using any mode other than fill requires enabling a GPU feature.
    // 如果用不是1.0f的值，需要开启 wide lines 功能
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    // multisampling设置
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

    if (GLOBAL_CONTROL_MSAA) {
      multisampling.rasterizationSamples = msaaSamples;
      if (GLOBAL_CONTROL_MSAA_SAMPLE_RATE_SHADING) {
        multisampling.sampleShadingEnable = VK_TRUE;
        multisampling.minSampleShading = GLOBAL_CONTROL_MSAA_MIN_SAMPLE_SHADING;
      } else {
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.minSampleShading = 1.0f;
      }
    } else {
      multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
      multisampling.sampleShadingEnable = VK_FALSE;
      multisampling.minSampleShading = 1.0f; // Optional
    }
    multisampling.pSampleMask = nullptr;            // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE;      // Optional

    // depth and stencil testing
    // 目前我们不用，创建时候指定nullptr即可

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;             // Optional

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    // Pipeline layout （用来绑定 uniform buffer）
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;    // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // 测试fragment是否通过
    depthStencil.depthTestEnable = VK_TRUE;
    // 测试通过后，是否写入depth
    depthStencil.depthWriteEnable = VK_TRUE;

    // 更小的深度值可以写入
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // 特殊能力，仅保留depth在某个区间的fragment 进行测试。
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional

    // 在拥有了 render pass， pipeline layout,
    // 以及上面关于pipeline的各种配置。 我们 终于可以 创建 graphics pipeline
    // 了
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    // 绑定shader stage
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    // 绑定输入状态定义（描述顶点输入的布局，包括顶点属性和顶点缓冲区绑定）
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    // 绑定输入装配状态定义（描述输入装配的模式，目前是三角形带）
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    // 绑定动态状态定义（描述动态状态，如视口、 scissor 区域等）
    // - 目前的动态状态：视口状态定义（描述视口和 scissor 区域。属于 dynamic
    // state）
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.pViewportState = &viewportState;
    // 绑定光栅化状态定义（描述光栅化的行为，如多边形模式、cull mode 等）
    pipelineInfo.pRasterizationState = &rasterizer;
    // 绑定多采样状态定义（描述多采样的行为，如样本数、alpha 到覆盖率等）
    pipelineInfo.pMultisampleState = &multisampling;
    // 绑定深度测试状态定义（描述深度测试的行为，如深度测试使能、比较操作等）
    pipelineInfo.pDepthStencilState = &depthStencil; // Optional
    // 绑定颜色混合状态定义（描述颜色混合的行为，如混合因子、混合操作等）
    pipelineInfo.pColorBlendState = &colorBlending;

    // 绑定layout
    pipelineInfo.layout = pipelineLayout;

    // 绑定 render pass （用来指定渲染目标和附件）
    // 这里需要一定的兼容性。
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0; // 绑定到 render pass 中的 subpass 0

    // 绑定 base pipeline （可选，用来继承之前的 pipeline 配置）
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1;              // Optional

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &graphicsPipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
  }

  VkShaderModule createShaderModule(const std::vector<char> &code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
  }

  bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName : validationLayers) {
      bool layerFound = false;
      for (const auto &layerProperties : availableLayers) {
        if (strcmp(layerName, layerProperties.layerName) == 0) {
          layerFound = true;
          break;
        }
      }

      if (!layerFound) {
        return false;
      }
    }

    return true;
  }

  std::vector<const char *> getRequiredExtenstions() {
    Uint32 extensionCount = 0;

    // 注意：必须在创建了 SDL_Window 并使用 SDL_WINDOW_VULKAN 标志之后调用
    const char *const *extensions =
        SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!extensions) {
      throw std::runtime_error(SDL_GetError());
    }

    // 拷贝到 std::vector 用于 VkInstanceCreateInfo
    std::vector<const char *> vkExtensions(extensions,
                                           extensions + extensionCount);

    if (enableValidationLayers) {
      vkExtensions.push_back((char *)VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return vkExtensions;
  }

  LX::SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
    LX::SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
                                              &details.capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                         nullptr);

    if (formatCount != 0) {
      details.formats.resize(formatCount);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                           details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                              &presentModeCount, nullptr);

    if (presentModeCount != 0) {
      details.presentModes.resize(presentModeCount);
      vkGetPhysicalDeviceSurfacePresentModesKHR(
          device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
  }

  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    for (const auto &availableFormat : availableFormats) {
      if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
          availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return availableFormat;
      }
    }

    return availableFormats[0];
  }

  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR> &availablePresentModes) {
    for (const auto &availablePresentMode : availablePresentModes) {
      if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return availablePresentMode;
      }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
      return capabilities.currentExtent;
    } else {
      int width, height;
      // 使用 SDL3 获取像素级 drawable 尺寸
      if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        throw std::runtime_error(
            "Failed to query window pixel size for Vulkan");
      }

      VkExtent2D actualExtent = {static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height)};

      actualExtent.width =
          std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                     capabilities.maxImageExtent.width);
      actualExtent.height =
          std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                     capabilities.maxImageExtent.height);

      return actualExtent;
    }
  }

  bool checkInstanceExtensionSupport() {
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> supportExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                           supportExtensions.data());

    std::cout << "vulkan available extensions:\n";

    for (const auto &extension : supportExtensions) {
      std::cout << '\t' << extension.extensionName << '\n';
    }

    // 用glfwGetRequiredInstanceExtensions 用来获取GLFW需要的实例扩展
    auto reqExtensions = getRequiredExtenstions();
    std::cout << "app required extensions:\n";
    for (uint32_t i = 0; i < reqExtensions.size(); i++) {
      std::cout << '\t' << reqExtensions[i] << '\n';
    }

    bool bRequiredSupported = true;
    for (uint32_t i = 0; i < reqExtensions.size(); i++) {
      bool extensionFound = false;
      for (const auto &extension : supportExtensions) {
        if (strcmp(reqExtensions[i], extension.extensionName) == 0) {
          extensionFound = true;
          break;
        }
      }
      if (!extensionFound) {
        bRequiredSupported = false;
        break;
      }
    }

    return bRequiredSupported;
  }

  void createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
      throw std::runtime_error(
          "validation layers requested, but not available!");
    }
    if (!checkInstanceExtensionSupport()) {
      throw std::runtime_error("GLFW required extensions not supported!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto reqExtensions = getRequiredExtenstions();
    // 这两个member用来指定启用的扩展
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(reqExtensions.size());
    createInfo.ppEnabledExtensionNames = reqExtensions.data();

    // 再往下的两个member实际是指定启用的layer。这里忽略了 ppEnabledLayerNames
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
      createInfo.enabledLayerCount =
          static_cast<uint32_t>(validationLayers.size());
      createInfo.ppEnabledLayerNames = validationLayers.data();

      // 如果想要调试 vkCreateInstance 调用，需要设置 pNext 指向 一个
      // VkDebugUtilsMessengerCreateInfoEXT 结构体
      populateDebugMessengerCreateInfo(debugCreateInfo);
      createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
      std::cout << "validation layers enabled" << std::endl;
    } else {
      createInfo.enabledLayerCount = 0;
    }

    // 最后可以创建了。保存在成员变量上。
    auto ret = vkCreateInstance(&createInfo, nullptr, &instance);
    if (ret != VK_SUCCESS) {
      std::cout << "create instance failed with code: " << ret << std::endl;
      throw std::runtime_error("failed to create instance!");
    }
  }

  VkDebugUtilsMessengerEXT debugMessenger;

  static VKAPI_ATTR VkBool32 VKAPI_CALL
  debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                VkDebugUtilsMessageTypeFlagsEXT messageType,
                const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                void *pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
  }

  void populateDebugMessengerCreateInfo(
      VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
    // 这个函数专门填充 VkDebugUtilsMessengerCreateInfoEXT 结构体
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
  }

  void setupDebugMessenger() {
    if (!enableValidationLayers)
      return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);
    createInfo.pUserData = nullptr; // Optional

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr,
                                     &debugMessenger) != VK_SUCCESS) {
      throw std::runtime_error("failed to set up debug messenger!");
    }
  }

  VkResult CreateDebugUtilsMessengerEXT(
      VkInstance instance,
      const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
      const VkAllocationCallbacks *pAllocator,
      VkDebugUtilsMessengerEXT *pDebugMessenger) {
    // 需要动态获取 vkCreateDebugUtilsMessengerEXT
    // 函数。因为这个函数是由扩展提供的。
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
      // 调用create函数，创建 debugMessenger handler
      return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
      return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
  }

  void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                     VkDebugUtilsMessengerEXT debugMessenger,
                                     const VkAllocationCallbacks *pAllocator) {
    // 同样，获取 vkDestroyDebugUtilsMessengerEXT 函数。
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
      // 调用destroy函数，销毁 debugMessenger handler
      func(instance, debugMessenger, pAllocator);
    }
  }

  void initWindow() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      throw std::runtime_error(SDL_GetError());
    }

    window = SDL_CreateWindow("RendererVulkanSDL3", WIDTH, HEIGHT,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
      throw std::runtime_error(SDL_GetError());
    }
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createCommandPool();
    createCommandBuffers();

    createColorResources();
    createDepthResources();
    createFramebuffers();

    createTextureImage();
    createTextureImageView();
    createTextureSampler(GLOBAL_CONTROL_MIPMAP);

    loadModel();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffer();
    createDescriptorPool();
    createDescriptorSets();
    createSyncObjects();

    initImGui(); // 依赖descriptorPool
  }

  void initImGui() {
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance;
    info.PhysicalDevice = physicalDevice;
    info.Device = device;

    LX::QueryQueueFamilyResult indices = findQueueFamilies(physicalDevice);
    info.QueueFamily = indices.graphicsFamily.value();
    info.Queue = graphicsQueue;

    info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    info.ImageCount = MAX_FRAMES_IN_FLIGHT;

    info.DescriptorPool = descriptorPool;
    info.DescriptorPoolSize = 0; // 0 表示不用 ImGui 自动创建
    // info.DescriptorPoolSize = dis

    ImGui_ImplVulkan_PipelineInfo pipelineInfoMain{};
    pipelineInfoMain.RenderPass = renderPass;
    pipelineInfoMain.Subpass = 0; // 主渲染子通道
    pipelineInfoMain.MSAASamples = msaaSamples;
    info.PipelineInfoMain = pipelineInfoMain;

    if (!ImGui_ImplVulkan_Init(&info)) {
      throw std::runtime_error("failed to initialize ImGui!");
    }
  }

  void cleanupSyncObjects() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
      vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
      vkDestroyFence(device, inFlightFences[i], nullptr);
    }
  }
  void createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    // 信号量用于同步渲染操作和 CPU 操作。
    // 信号量可以在命令缓冲区中使用 waitSemaphore 指令等待，
    // 并在渲染完成后使用 signalSemaphore 指令通知。
    // 这允许我们在渲染完成后才执行后续操作，避免了渲染和 CPU
    // 操作之间的竞争条件。
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // 围栏用于同步渲染操作和 CPU 操作。
    // 围栏可以在命令缓冲区中使用 waitFence 指令等待，
    // 并在渲染完成后使用 signalFence 指令通知。
    // 这允许我们在渲染完成后才执行后续操作，避免了渲染和 CPU
    // 操作之间的竞争条件。
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; //! 防止第一帧开始就阻塞了！

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                            &imageAvailableSemaphores[i]) != VK_SUCCESS ||
          vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                            &renderFinishedSemaphores[i]) != VK_SUCCESS ||
          vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) !=
              VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphores!");
      }
    }
  }

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  VkQueue computeQueue;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;

  VkImageView createImageView(VkImage image, VkFormat format,
                              VkImageAspectFlags aspectFlags,
                              uint32_t mipLevels) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create image view!");
    }

    return imageView;
  }

  void createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
      swapChainImageViews[i] =
          createImageView(swapChainImages[i], swapChainImageFormat,
                          VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
  }

  void cleanupSwapChain() {
    vkDestroyImageView(device, colorImageView, nullptr);
    vkDestroyImage(device, colorImage, nullptr);
    vkFreeMemory(device, colorImageMemory, nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);

    for (auto framebuffer : swapChainFramebuffers) {
      vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    for (auto imageView : swapChainImageViews) {
      vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
  }

  void recreateSwapChain() {
    int width = 0, height = 0;

    // 获取窗口像素尺寸
    SDL_GetWindowSizeInPixels(window, &width, &height);

    // 如果窗口被最小化，像素尺寸可能为 0，需要等待
    SDL_Event event;
    while (width == 0 || height == 0) {
      // 等待事件
      if (SDL_WaitEvent(&event)) {
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
          SDL_GetWindowSizeInPixels(window, &width, &height);
        } else if (event.type == SDL_EVENT_QUIT) {
          return; // 用户关闭窗口
        }
      }
    }

    vkDeviceWaitIdle(device);

    cleanupSwapChain();
    cleanupSyncObjects();

    createSwapChain();
    createImageViews();
    createColorResources();
    createDepthResources();
    createFramebuffers();

    createSyncObjects();
  }

  void createSwapChain() {
    LX::SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(physicalDevice);
    VkSurfaceFormatKHR surfaceFormat =
        chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode =
        chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
      imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    // createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    LX::QueryQueueFamilyResult indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
                                     indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
      // 如果queue不同，采用这个模式，就可以避免显示的所有权转移（相当于两个queue公用一个image）
      createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      createInfo.queueFamilyIndexCount = 2;
      createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
      // queue相同，那么不用特殊处理，采用默认的exclusive模式
      createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      createInfo.queueFamilyIndexCount = 0;     // Optional
      createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    // 预先变换。比如预先对窗口旋转了90度，那么就需要设置为VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR
    // 这里设置为currentTransform，就是说窗口的变换和swapchain的变换保持一致
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;

    // 合成alpha通道。这里设置为opaque，就是说swapchain的image不会有透明通道
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    createInfo.presentMode = presentMode;
    // 是否裁剪。这里设置为VK_TRUE，就是说如果swapchain的image超出了窗口的范围，那么就会被裁剪
    // 如果被其他窗口遮挡，那么也会被裁剪
    createInfo.clipped = VK_TRUE;

    // 使用 Vulkan 时，
    // 你的交换链在应用程序运行过程中可能会变得无效或未优化，例如因为窗口被调整大小。
    // 在这种情况下，交换链实际上需要从头开始重新创建，并且必须在此字段中指定对旧交换链的引用。
    // 这是一个复杂的话题，我们将在未来的章节中了解更多。目前，我们将假设我们只会创建一个交换链。
    //! 优化resize问题应该看这里了。
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount,
                            swapChainImages.data());

    MAX_FRAMES_IN_FLIGHT = imageCount;

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
  }

  void createSurface() {
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create window surface!");
    }
  }

  void createLogicalDevice() {
    LX::QueryQueueFamilyResult indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
                                              indices.presentFamily.value()};
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
      VkDeviceQueueCreateInfo queueCreateInfo{};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = queueFamily;
      queueCreateInfo.queueCount = 1;
      queueCreateInfo.pQueuePriorities = &queuePriority;
      queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    // 开启geometry shader
    deviceFeatures.geometryShader = useGeometryShader ? VK_TRUE : VK_FALSE;
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    if (GLOBAL_CONTROL_MSAA && GLOBAL_CONTROL_MSAA_SAMPLE_RATE_SHADING) {
      deviceFeatures.sampleRateShading = VK_TRUE;
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount =
        static_cast<uint32_t>(queueCreateInfos.size());

    createInfo.pEnabledFeatures = &deviceFeatures;

    // 开启扩展
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create logical device!");
    }

    // 获取graphics queue
    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    // 获取present queue
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);

    vkGetDeviceQueue(device, indices.graphicsAndComputeFamily.value(), 0,
                     &computeQueue);
  }

  VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

  VkSampleCountFlagBits getMaxUsableSampleCount() {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

    VkSampleCountFlags counts =
        physicalDeviceProperties.limits.framebufferColorSampleCounts &
        physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) {
      return VK_SAMPLE_COUNT_64_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_32_BIT) {
      return VK_SAMPLE_COUNT_32_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_16_BIT) {
      return VK_SAMPLE_COUNT_16_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_8_BIT) {
      return VK_SAMPLE_COUNT_8_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_4_BIT) {
      return VK_SAMPLE_COUNT_4_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_2_BIT) {
      return VK_SAMPLE_COUNT_2_BIT;
    }

    return VK_SAMPLE_COUNT_1_BIT;
  }

  void pickPhysicalDevice() {
    std::vector<LX::DeviceScore> scoredDevices;

    // 先获取设备数量
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
      throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    // 再获取设备列表
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // 打印，然后我们手动指定一个来pick
    std::cout << "Available devices Count:" << deviceCount << std::endl;
    for (const auto &device : devices) {
      LX::DeviceScore ds{};
      ds.device = device;

      // 4a. 获取设备属性和驱动属性
      VkPhysicalDeviceDriverProperties driverProps{};
      driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

      // VkPhysicalDeviceProperties2 props2{};
      // props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      // props2.pNext = &driverProps;
      // vkGetPhysicalDeviceProperties2(device, &props2);

      // ds.properties = props2.properties;
      ds.driverProperties = driverProps;

      const auto &props = ds.properties;

      ds.suitable = isDeviceSuitable(device);
      if (ds.suitable) {
        // 基础分数：独立的 GPU 总是优先
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
          ds.score += 1000; // 独立显卡给高分
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
          ds.score += 100; // 集成显卡次之
        }

        // 次要分数：根据限制性特性进行加分
        // 更多的内存、更好的限制（如 maxImageDimension2D）等可以作为加分项
        ds.score += props.limits.maxImageDimension2D / 1024; // 图像尺寸越大越好
      }

      scoredDevices.push_back(ds);
    }
    auto bestDevice =
        std::max_element(scoredDevices.begin(), scoredDevices.end(),
                         [](const LX::DeviceScore &a, const LX::DeviceScore &b) {
                           return a.score < b.score;
                         });
    // 6. 选择最佳设备
    if (bestDevice != scoredDevices.end()) {
      physicalDevice = bestDevice->device;
      if (GLOBAL_CONTROL_MSAA) {
        msaaSamples = getMaxUsableSampleCount();
      } else {
        msaaSamples = VK_SAMPLE_COUNT_1_BIT;
      }
      std::cout << "\n Successfully picked best device: "
                << bestDevice->properties.deviceName
                << " (Score: " << bestDevice->score << ")" << std::endl;
      return;
    }
    // 7. 如果没有找到合适的设备
    throw std::runtime_error("failed to find a suitable GPU!");
  }

  bool isDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    // 检查是否支持geometry shader
    if (useGeometryShader && !deviceFeatures.geometryShader) {
      std::cout << "Device " << deviceProperties.deviceName
                << " does not support geometry shader!" << std::endl;
      return false;
    }

    if (!checkDeviceExtensionSupport(device)) {
      std::cout << "Device " << deviceProperties.deviceName
                << " does not support required extensions!" << std::endl;
      return false;
    }

    bool swapChainAdequate = false;
    LX::SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    swapChainAdequate = !swapChainSupport.formats.empty() &&
                        !swapChainSupport.presentModes.empty();
    if (!swapChainAdequate) {
      std::cout << "Device " << deviceProperties.deviceName
                << " swap chain: format or present mode empty!" << std::endl;
      return false;
    }

    auto indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
      return false;
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
    // 检查是否支持采样器_anisotropy
    if (!supportedFeatures.samplerAnisotropy) {
      std::cout << "Device " << deviceProperties.deviceName
                << " does not support sampler anisotropy!" << std::endl;
      return false;
    }

    return true;
  }

  // 检查设备是否支持交换链扩展
  bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    // 1. 获取设备支持的扩展
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                         nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                         availableExtensions.data());

    // optional 打印一下所有支持的扩展
    // std::cout << "Available device extensions:" << std::endl;
    // for (const auto& ext : availableExtensions) {
    //     std::cout << ext.extensionName << std::endl;
    // }

    std::set<std::string> requiredExtensions(deviceExtensions.begin(),
                                             deviceExtensions.end());

    for (const auto &extension : availableExtensions) {
      requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();

    //// 规模大的时候，下面的才会快
    // std::unordered_set<std::string> availableSet;
    // for (const auto& ext : availableExtensions) {
    //     availableSet.insert(ext.extensionName);
    // }

    // // 3. 检查所有必需扩展是否都存在
    // for (const char* required : deviceExtensions) {
    //     if (availableSet.find(required) == availableSet.end()) {
    //         return false; // 有一个必需扩展不存在
    //     }
    // }

    // return true; // 所有必需扩展都存在
  }

  LX::QueryQueueFamilyResult findQueueFamilies(VkPhysicalDevice physicalDevice) {
    LX::QueryQueueFamilyResult indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies.data());

    // 遍历队列族，查找支持图形和呈现的队列族索引
    int i = 0;
    for (const auto &queueFamily : queueFamilies) {
      if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        indices.graphicsFamily = i;
      }
      if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
          (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        indices.graphicsAndComputeFamily = i;
      }
      VkBool32 presentSupport = false;
      // 因为呈现能力，需要测试：物理设备、队列族索引、呈现表面这三个都需要有。
      vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface,
                                           &presentSupport);
      if (presentSupport) {
        indices.presentFamily = i;
      }

      // 注: 如果 支持呈现的索引和
      // graphics索引是同一个，那么意味着这个队列即可以提交绘制命令，也可以提交显示指令

      if (indices.isComplete()) {
        break;
      }
      i++;
    }
    return indices;
  }

  std::atomic<bool> shouldClose{false};
  std::thread renderThread;
  std::atomic<bool> renderPaused{false};

  void renderThreadFunc() {
    auto lastTime = std::chrono::steady_clock::now();

    while (!shouldClose.load()) {
      if (!renderPaused.load()) {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - lastTime);

        if (elapsed.count() >= 8) { // 约120fps
          try {
            drawFrame();
          } catch (const std::exception &e) {
            std::cerr << "Render error: " << e.what() << std::endl;
          }
          lastTime = currentTime;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ImDrawData *drawDataForRenderThread = nullptr;
  std::mutex drawDataMutex;

  // 控制主循环是否退出
  void mainLoop() {
    // 启动渲染线程
    renderThread =
        std::thread(&HelloTriangleApplication::renderThreadFunc, this);

    SDL_Event event;
    // 主线程专门处理事件
    while (!shouldClose.load()) {
      bool uiMayUpdated = false;
      // 等待事件，如果没有事件会阻塞
      while (SDL_PollEvent(&event)) {
        if(ImGui_ImplSDL3_ProcessEvent(&event)){
          uiMayUpdated = true;
        }
        switch (event.type) {
        case SDL_EVENT_QUIT:
          shouldClose.store(true);
          break;

        // SDL3 窗口像素大小变化事件
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          framebufferResized = true;
          break;

        default:
          break;
        }
      }

      // 获取窗口当前像素尺寸，决定是否暂停渲染
      int width = 0, height = 0;
      if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        throw std::runtime_error("Failed to get window pixel size");
      }
      ImGuiIO &io = ImGui::GetIO();
      io.DisplaySize = ImVec2((float)width, (float)height);
      if (uiMayUpdated) {
        std::lock_guard<std::mutex> lock(drawDataMutex);
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        ImGui::ShowDemoWindow(); // 自定义UI
        ImGui::Render();
        drawDataForRenderThread = ImGui::GetDrawData();
      }
      renderPaused.store(width == 0 || height == 0);

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (renderThread.joinable()) {
      renderThread.join();
    }

    vkDeviceWaitIdle(device);
  }

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;

  bool framebufferResized = false;

  uint32_t currentFrame = 0;

  void updateUniformBuffer(uint32_t frameIndex, bool rotate = true) {
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(
                     currentTime - startTime)
                     .count();

    UniformBufferObject ubo{};
    if (!rotate) {
      ubo.model = glm::mat4(1.0f);
    } else {
      ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                              glm::vec3(0.0f, 0.0f, 1.0f));
    }
    // 视图变换：摄像机从 (2,2,2) 看向原点
    ubo.view =
        glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f));
    // 投影变换：45° 视角，近平面0.1，远平面10
    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 10.0f);
    // 由于GLM库默认给OpenGL设计的，Vulkan里需要flip Y坐标。
    ubo.proj[1][1] *= -1;

    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
  }

  void drawFrame() {
    // 等待上一帧完成（注意，第一帧还没画的时候，会直接阻塞死，因此要在
    // createSyncObject 即初始化位置处理一下）
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
                    UINT64_MAX);

    // 获取接下来渲染的image索引
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        framebufferResized) {
      framebufferResized = false;
      recreateSwapChain();
      return;
    } else if (result != VK_SUCCESS) {
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    // 重置fence，让其继续生效（可以下一帧阻塞主线程）。应该立马调用，因为后面的渲染还会用到它。
    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    // 重置 command buffer，准备新的绘制命令
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    // 绘图
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    // 更新uniform buffer(只要在提交前更新就行)
    updateUniformBuffer(currentFrame, GLOBAL_CONTROL_ROTATE);

    // 准备提交 command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submitInfo.waitSemaphoreCount = 1;
    // 提交中的同步设定：下面这两个一一对应，管线执行到上面的stage，就wait下面的semaphore
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.pWaitSemaphores = waitSemaphores;

    // 具体提交的 command buffer
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    // 执行结束后，signal下面的semaphore；主要通知GPU其他队列或者 swap chain 等
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // 这里是，当command buffer执行完后，signal下面的semaphore
    // 注意，提交结束就释放了fence，这导致 presentQueue 可能还在使用
    // renderFinishedSemaphore 的时候就进入了下一帧渲染，从而触发了
    // renderFinishedSemaphore
    // 的使用。但validator无法判断这么细的逻辑，只看到两个队列使用semaphone于是报错。
    // 这里有个讨论帖子：
    // https://www.reddit.com/r/vulkan/comments/1me8ubj/vulkan_validation_error/
    // 最好做法还是，直接每个swap chain image具备一套信号量。
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                      inFlightFences[currentFrame]) != VK_SUCCESS) {
      throw std::runtime_error("failed to submit draw command buffer!");
    }

    // 呈现前等待信号量（renderFinish)
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    // 要呈现的 swap chain 的配置
    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    // 呈现调用的返回值。当多 swap chain的时候使用。单个可以用返回值。
    presentInfo.pResults = nullptr; // Optional

    // 呈现 swap chain 中的 image
    vkQueuePresentKHR(presentQueue, &presentInfo);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
  }

  void cleanup() {
    cleanupSwapChain();

    vkDestroyImageView(device, textureImageView, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroyBuffer(device, uniformBuffers[i], nullptr);
      vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
    }

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);

    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    vkDestroyRenderPass(device, renderPass, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
      vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
      vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDevice(device, nullptr);

    if (enableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    SDL_DestroyWindow(window);

    SDL_Quit();
  }
};

int main() {
  expSetEnvVK();
  std::cout << "Hello Triangle!" << std::endl;

  HelloTriangleApplication app;

  try {
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Goodbye Triangle!" << std::endl;
  return EXIT_SUCCESS;
}
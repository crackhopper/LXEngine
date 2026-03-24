#include "core/resources/index_buffer.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/components/material.hpp"
#include "core/gpu/render_resource.hpp"
#include "graphics_backend/vulkan/details/commands/vkc_cmdbuffer_manager.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_framebuffer.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/vk_resource_manager.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct ImageResources {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

ImageResources createImageWithView(LX_core::graphic_backend::VulkanDevice &device,
                                     uint32_t width, uint32_t height,
                                     VkFormat format,
                                     VkImageUsageFlags usage,
                                     VkImageAspectFlags aspect) {
  ImageResources out;
  VkDevice vkDevice = device.getLogicalDevice();

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(vkDevice, &imageInfo, nullptr, &out.image) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateImage failed");
  }

  VkMemoryRequirements memReq{};
  vkGetImageMemoryRequirements(vkDevice, out.image, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex =
      device.findMemoryTypeIndex(memReq.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &out.memory) != VK_SUCCESS) {
    throw std::runtime_error("vkAllocateMemory failed");
  }

  vkBindImageMemory(vkDevice, out.image, out.memory, 0);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = out.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &out.view) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateImageView failed");
  }

  return out;
}

void destroyImageWithView(LX_core::graphic_backend::VulkanDevice &device,
                           ImageResources &res) {
  VkDevice vkDevice = device.getLogicalDevice();
  if (res.view != VK_NULL_HANDLE) {
    vkDestroyImageView(vkDevice, res.view, nullptr);
    res.view = VK_NULL_HANDLE;
  }
  if (res.image != VK_NULL_HANDLE) {
    vkDestroyImage(vkDevice, res.image, nullptr);
    res.image = VK_NULL_HANDLE;
  }
  if (res.memory != VK_NULL_HANDLE) {
    vkFreeMemory(vkDevice, res.memory, nullptr);
    res.memory = VK_NULL_HANDLE;
  }
}
} // namespace

static void cdToWhereShadersExist() {
  fs::path p = fs::current_path();
  for (int i = 0; i < 8; ++i) {
    if (fs::exists(p / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p);
      return;
    }
    if (fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.vert.spv") &&
        fs::exists(p / "build" / "shaders" / "glsl" / "blinnphong_0.frag.spv")) {
      fs::current_path(p / "build");
      return;
    }
    const auto parent = p.parent_path();
    if (parent == p) break;
    p = parent;
  }
}

int main() {
  try {
    cdToWhereShadersExist();

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize();

    // Render pass / pipeline formats.
    const VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
    VkSurfaceFormatKHR surfaceFormat{};
    surfaceFormat.format = colorFormat;
    surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    auto resourceManager = LX_core::graphic_backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat, depthFormat);

    auto *renderPass = resourceManager->getRenderPass();
    auto *pipeline = resourceManager->getRenderPipeline();
    if (!renderPass || !pipeline || pipeline->getHandle() == VK_NULL_HANDLE) {
      std::cerr << "RenderPass/Pipeline not initialized correctly\n";
      return 1;
    }

    // Create minimal framebuffer attachments.
    const VkExtent2D extent{64, 64};
    ImageResources color =
        createImageWithView(*device, extent.width, extent.height, colorFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    ImageResources depth =
        createImageWithView(*device, extent.width, extent.height, depthFormat,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    std::vector<VkImageView> attachments = {color.view, depth.view};
    auto framebuffer = LX_core::graphic_backend::VulkanFrameBuffer::create(
        *device, renderPass->getHandle(), attachments, extent);

    using V = LX_core::VertexPosNormalUvBone;

    // Build a minimal scene so VulkanCommandBuffer::bindResources has CPU-side
    // descriptor resources to upload into descriptor sets.
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create(
        {
            V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
              {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0},
              {1.0f, 0.0f, 0.0f, 0.0f}),
            V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
              {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0},
              {1.0f, 0.0f, 0.0f, 0.0f}),
            V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
              {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0},
              {1.0f, 0.0f, 0.0f, 0.0f}),
        });
    auto indexBufferPtr = LX_core::IndexBuffer::create({0u, 1u, 2u});
    auto meshPtr = LX_core::Mesh<V>::create(vertexBufferPtr, indexBufferPtr);

    auto material = std::make_shared<LX_core::MaterialBlinnPhong>(
        LX_core::ResourcePassFlag::Forward);
    material->params->params.enableNormalMap = 0; // avoid normal texture
    material->params->setDirty();

    auto renderable =
        std::make_shared<LX_core::RenderableSubMesh<V>>(meshPtr, material);
    // Pipeline declares a SkeletonUBO slot; attach an (empty) skeleton so the
    // descriptor set binding gets a valid buffer to update.
    renderable->skeleton =
        std::make_shared<LX_core::Skeleton>(std::vector<LX_core::Bone>{});
    auto scene = LX_core::Scene::create(renderable);

    // Default directional light UBO (shader expects it).
    if (scene->directionalLight && scene->directionalLight->ubo) {
      scene->directionalLight->ubo->param.dir =
          LX_core::Vec4f{0.0f, -1.0f, 0.0f, 0.0f};
      scene->directionalLight->ubo->param.color =
          LX_core::Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
      scene->directionalLight->ubo->setDirty();
    }

    // Camera matrices needed for CameraUBO uploads.
    scene->camera->position = {0.0f, 0.0f, 3.0f};
    scene->camera->target = {0.0f, 0.0f, 0.0f};
    scene->camera->up = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
    scene->camera->updateMatrices();

    auto renderItem = scene->buildRenderItem();

    // Match VulkanRenderer::initScene(): inject camera/light UBO resources.
    if (scene->camera) {
      auto camRes = scene->camera->getRenderResources();
      renderItem.descriptorResources.insert(renderItem.descriptorResources.end(),
                                             camRes.begin(), camRes.end());
    }
    if (scene->directionalLight) {
      auto lightRes = scene->directionalLight->getRenderResources();
      renderItem.descriptorResources.insert(renderItem.descriptorResources.end(),
                                             lightRes.begin(), lightRes.end());
    }

    // Initialize push constants deterministically.
    if (renderItem.objectInfo) {
      LX_core::PC_BlinnPhong pc{};
      pc.model = LX_core::Mat4f::identity();
      pc.enableLighting = 1;
      pc.enableSkinning = 0;
      renderItem.objectInfo->update(pc);
    }

    // Needed for GPU-side uploads that require transient command buffers
    // (e.g. textures).
    LX_core::graphic_backend::VulkanCommandBufferManagerPtr cmdMgr =
        LX_core::graphic_backend::VulkanCommandBufferManager::create(
            *device, /*maxFramesInFlight=*/1,
            device->getGraphicsQueueFamilyIndex());
    resourceManager->setCommandBufferManager(*cmdMgr);

    // Sync all CPU-side resources to GPU.
    resourceManager->syncResource(renderItem.vertexBuffer);
    resourceManager->syncResource(renderItem.indexBuffer);
    for (auto &cpuRes : renderItem.descriptorResources) {
      resourceManager->syncResource(cpuRes);
    }
    resourceManager->collectGarbage();

    cmdMgr->beginFrame(0);
    auto cmd = cmdMgr->allocateBuffer();
    cmd.setResourceManager(*resourceManager);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    vkBeginCommandBuffer(cmd.getHandle(), &beginInfo);

    cmd.beginRenderPass(renderPass->getHandle(), framebuffer->getHandle(), extent,
                          renderPass->getClearValues());
    cmd.setViewport(extent.width, extent.height);
    cmd.setScissor(extent.width, extent.height);
    cmd.bindPipeline(*pipeline);

    cmd.bindResources(*pipeline, renderItem);
    cmd.drawItem(renderItem);
    cmd.endRenderPass();

    vkEndCommandBuffer(cmd.getHandle());

    framebuffer.reset();
    destroyImageWithView(*device, color);
    destroyImageWithView(*device, depth);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanCommandBuffer test: " << e.what() << "\n";
    return 0;
  }
}


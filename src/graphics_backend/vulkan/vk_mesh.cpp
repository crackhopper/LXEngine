#include "vk_mesh.hpp"
#include "core/resources/mesh.hpp"
#include "details/vk_cmdbuffer.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace LX_core::graphic_backend {

void VulkanMesh::upload(VulkanDevice &device, const LX_core::Mesh &meshData) {
  std::vector<uint8_t> mergedVertices;
  std::vector<uint8_t> mergedIndices;
  uint32_t totalIndexCount = 0;

  for (size_t i = 0; i < meshData.subMeshCount(); ++i) {
    const auto *sub = meshData.subMesh(i);

    if (const auto *vb = sub->vertexBuffer()) {
      auto *src = static_cast<const uint8_t *>(vb->data());
      mergedVertices.insert(mergedVertices.end(), src, src + vb->size());
    }

    if (const auto *ib = sub->indexBuffer()) {
      auto *src = static_cast<const uint8_t *>(ib->data());
      mergedIndices.insert(mergedIndices.end(), src, src + ib->size());
      totalIndexCount += static_cast<uint32_t>(ib->indexCount());
    }
  }

  indexCount = totalIndexCount;

  VkDeviceSize vSize = mergedVertices.size();
  VkDeviceSize iSize = mergedIndices.size();

  if (vSize == 0 || iSize == 0)
    return;

  auto vStaging =
      VulkanBuffer::create(device, vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vStaging->uploadData(mergedVertices.data(), vSize);

  auto iStaging =
      VulkanBuffer::create(device, iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  iStaging->uploadData(mergedIndices.data(), iSize);

  vertexBuffer = VulkanBuffer::create(device, vSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  indexBuffer = VulkanBuffer::create(device, iSize,
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  auto cmd =
      device.newCmdBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  cmd->begin();

  vStaging->copyTo(*cmd, *vertexBuffer);
  iStaging->copyTo(*cmd, *indexBuffer);

  cmd->end();

  VkCommandBuffer rawCmd = cmd->getHandle();
  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &rawCmd;

  vkQueueSubmit(device.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(device.getGraphicsQueue());
}

} // namespace LX_core::graphic_backend

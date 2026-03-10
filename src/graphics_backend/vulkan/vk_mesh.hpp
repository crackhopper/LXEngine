#pragma once
#include "details/vk_resources.hpp"
#include "vk_device.hpp"
#include <memory>
namespace LX_core::graphic_backend {
class VulkanMesh;
using VulkanMeshPtr = std::unique_ptr<VulkanMesh>;
class VulkanMesh {
  struct Token {};

public:
  VulkanMesh(Token) {}

  static VulkanMeshPtr create(VulkanDevice &device,
                              const LX_core::Mesh &meshData) {
    auto mesh = std::make_unique<VulkanMesh>(Token{});
    mesh->upload(device, meshData);
    return mesh;
  }
  const VulkanBuffer &getVertexBuffer() const { return *vertexBuffer; }
  const VulkanBuffer &getIndexBuffer() const { return *indexBuffer; }

  VulkanBuffer &getVertexBuffer() { return *vertexBuffer; }
  VulkanBuffer &getIndexBuffer() { return *indexBuffer; }
  uint32_t getIndexCount() const { return indexCount; }

private:
  VulkanBufferPtr vertexBuffer;
  VulkanBufferPtr indexBuffer;
  uint32_t indexCount = 0;

  void upload(VulkanDevice &device, const LX_core::Mesh &meshData);
};
} // namespace LX_core::graphic_backend
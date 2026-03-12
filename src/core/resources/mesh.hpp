#pragma once
#include "buffer.hpp"
#include "material.hpp"
#include "skeleton.hpp"
#include "vertex.hpp"
#include "../math/mat.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace LX_core {
// SubMesh 。拆分目的为了材质/DrawCall优化预留。
class SubMesh {
public:
  SubMesh(std::unique_ptr<Buffer> vertexBuffer,
          std::unique_ptr<IndexBuffer> indexBuffer)
      : m_vertexBuffer(std::move(vertexBuffer)),
        m_indexBuffer(std::move(indexBuffer)) {}

  const Buffer *vertexBuffer() const { return m_vertexBuffer.get(); }
  const IndexBuffer *indexBuffer() const { return m_indexBuffer.get(); }

  void offsetIndexBuffer(uint32_t offset) { m_indexBuffer->offset(offset); }

private:
  std::unique_ptr<Buffer> m_vertexBuffer;
  std::unique_ptr<IndexBuffer> m_indexBuffer;

  std::shared_ptr<MaterialBase> m_material;
};

struct SkinData {
  std::vector<int> boneIndices;   // 每个顶点的骨骼索引
  std::vector<float> boneWeights; // 每个顶点的骨骼权重
};

// 多个网格，加可选的skinning数据
class Mesh;
using MeshPtr = std::shared_ptr<Mesh>;
class Mesh {
public:
  Mesh() = default;

  std::unique_ptr<SubMesh> &lastSubMesh() { return m_subMeshes.back(); }
  // 添加子网格
  void addSubMesh(std::unique_ptr<SubMesh> subMesh) {
    subMesh->offsetIndexBuffer(lastSubMesh()->indexBuffer()->maxIndex() + 1);
    m_subMeshes.push_back(std::move(subMesh));
  }

  // 获取子网格数量
  size_t subMeshCount() const { return m_subMeshes.size(); }

  // 获取子网格
  const SubMesh *subMesh(size_t i) const { return m_subMeshes[i].get(); }

  // 可选 Skinning 数据
  void setSkinningData(SkinData &&skin) { m_skinData = std::move(skin); }
  const std::optional<SkinData> &skinningData() const { return m_skinData; }

  // std140 对齐
  struct alignas(16) UBO {
    Mat4f modelMatrix = Mat4f::identity();
    int enableSkinning = 0;
    int padding[3] = {0, 0, 0}; // 填充到 16-byte 对齐 
  };

  UBO ubo;

private:
  // 注意：子网格所用的索引不能重合。这点在添加进来的时候需要处理。
  std::vector<std::unique_ptr<SubMesh>> m_subMeshes;
  std::optional<SkinData> m_skinData; // 可选

  std::shared_ptr<Skeleton> m_skeleton;
};

} // namespace LX_core
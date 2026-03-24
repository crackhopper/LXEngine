#pragma once
#include "core/gpu/render_resource.hpp"
#include <algorithm>
#include <memory>
#include <vector>
namespace LX_core {

// 索引缓冲
class IndexBuffer : public IRenderResource {
public:
  using Ptr = std::shared_ptr<IndexBuffer>;

  IndexBuffer(std::vector<uint32_t> &&indices,
              ResourcePassFlag passFlag = ResourcePassFlag::Forward)
      : m_indices(std::move(indices)), m_passFlag(passFlag) {
    if (!m_indices.empty()) {
      auto [minIt, maxIt] =
          std::minmax_element(m_indices.begin(), m_indices.end());
      m_minIndex = *minIt;
      m_maxIndex = *maxIt;
    } else {
      m_minIndex = m_maxIndex = 0;
    }
  }

  static Ptr create(std::vector<uint32_t> &&indices,
                    ResourcePassFlag passFlag = ResourcePassFlag::Forward) {
    return std::make_shared<IndexBuffer>(std::move(indices), passFlag);
  }

  void update(const std::vector<u32> &indices) {
    m_indices = indices;
    setDirty();
  }

  size_t indexCount() const { return m_indices.size(); }

  ResourcePassFlag getPassFlag() const override { return m_passFlag; }
  ResourceType getType() const override { return ResourceType::IndexBuffer; }
  const void *getRawData() const override { return m_indices.data(); }  
  u32 getByteSize() const override { return m_indices.size() * sizeof(u32);}


  u32 maxIndex() const { return m_maxIndex; }
  // 偏移索引值
  void offset(uint32_t offset) {
    for (auto &index : m_indices) {
      index += offset;
    }
    m_maxIndex += offset;
    m_minIndex += offset;
    setDirty();
  }

  void resetOffset() {
    for (auto &index : m_indices) {
      index -= m_minIndex;
    }
    m_maxIndex -= m_minIndex;
    m_minIndex = 0;
  }

private:
  std::vector<uint32_t> m_indices;
  uint32_t m_maxIndex;
  uint32_t m_minIndex;
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};

using IndexBufferPtr = std::shared_ptr<IndexBuffer>;
}  // namespace LX_core
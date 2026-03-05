#pragma once
#include "../math/vec.hpp"
#include <algorithm>
#include <vector>

namespace LX_core {

class Buffer {
public:
  virtual ~Buffer() = default;
  virtual const void *data() const = 0;
  virtual size_t size() const = 0;
};

// 顶点缓冲
template <typename VertexType> class VertexBuffer : public Buffer {
public:
  VertexBuffer(std::vector<VertexType> &&vertices)
      : m_vertices(std::move(vertices)) {}
  VertexBuffer(std::initializer_list<VertexType> list) : m_vertices(list) {}

  const void *data() const override { return m_vertices.data(); }
  size_t size() const override {
    return m_vertices.size() * sizeof(VertexType);
  }
  size_t vertexCount() const { return m_vertices.size(); }

private:
  std::vector<VertexType> m_vertices;
};

// 索引缓冲
class IndexBuffer : public Buffer {
public:
  IndexBuffer(std::vector<u32> &&indices) : m_indices(std::move(indices)) {
    if (!m_indices.empty()) {
      auto [minIt, maxIt] =
          std::minmax_element(m_indices.begin(), m_indices.end());
      m_minIndex = *minIt;
      m_maxIndex = *maxIt;
    } else {
      m_minIndex = m_maxIndex = 0;
    }
  }

  const void *data() const override { return m_indices.data(); }
  size_t size() const override { return m_indices.size() * sizeof(u32); }
  size_t indexCount() const { return m_indices.size(); }

  u32 maxIndex() const { return m_maxIndex; }
  // 偏移索引值
  void offset(uint32_t offset) {
    for (auto &index : m_indices) {
      index += offset;
    }
    m_maxIndex += offset;
    m_minIndex += offset;
  }

  void resetOffset() {
    for (auto &index : m_indices) {
      index -= m_minIndex;
    }
    m_maxIndex -= m_minIndex;
    m_minIndex = 0;
  }

private:
  std::vector<u32> m_indices;
  u32 m_maxIndex;
  u32 m_minIndex;
};

} // namespace LX_core
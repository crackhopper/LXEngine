module;
#include <algorithm>
#include <vector>
#include <cstdlib>
#include <cstring>

export module LX_New_Common.Memory:RawBuffer;

import LX_New_Common.Platform;

export namespace LX_New_Common {

// RawBuffer — 连续内存分配器，为可变长 GPU 资源提供底层存储。
//
// 关键特性：
//   - m_data 首地址按 kBaseAlignment (256 字节) 对齐，可直接用于 GPU 上传
//   - 内部使用 _aligned_malloc / std::aligned_alloc 管理内存
//   - 分配策略：first-fit + 相邻空闲块自动合并
//
// 示例：
//   RawBuffer buf;
//   u32 off = buf.allocate(128, 16);   // 分配 128 字节，16 字节对齐
//   u8* ptr = buf.getBase() + off;      // getBase() 返回 256 字节对齐的指针
//   std::memcpy(ptr, myData, 128);
//   buf.free(off, 128);                  // 归还内存，自动合并相邻空闲块

class RawBuffer {
  u8* m_data = nullptr;
  u32 m_size = 0;     // 已使用字节数
  u32 m_capacity = 0; // 已分配总字节数

  // 空闲块链表，按 offset 排序，释放时合并相邻块。
  struct FreeBlock { u32 offset; u32 size; };
  std::vector<FreeBlock> m_freeBlocks;

  static constexpr u32 kInitialCapacity = 1024 * 1024; // 首次分配时预扩到 1MB
  static constexpr u32 kBaseAlignment = 256;           // getBase() 的对齐保证

  static u8* alignedAlloc(u32 size, u32 alignment) {
#ifdef _MSC_VER
    return static_cast<u8*>(_aligned_malloc(size, alignment));
#else
    u32 alignedSize = (size + alignment - 1) & ~(alignment - 1);
    return static_cast<u8*>(std::aligned_alloc(alignment, alignedSize));
#endif
  }

  static void alignedFree(u8* p) {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
  }

  // 扩容：分配新的对齐内存块，拷贝旧数据，释放旧块。
  void grow(u32 newCapacity) {
    u8* newData = alignedAlloc(newCapacity, kBaseAlignment);
    if (m_data && m_size > 0) {
      std::memcpy(newData, m_data, m_size);
    }
    if (m_data) alignedFree(m_data);
    m_data = newData;
    m_capacity = newCapacity;
  }

public:
  RawBuffer() = default;
  RawBuffer(const RawBuffer&) = delete;
  RawBuffer& operator=(const RawBuffer&) = delete;

  RawBuffer(RawBuffer&& other) noexcept
      : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity),
        m_freeBlocks(std::move(other.m_freeBlocks)) {
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
  }
  RawBuffer& operator=(RawBuffer&& other) noexcept {
    if (this != &other) {
      if (m_data) alignedFree(m_data);
      m_data = other.m_data;
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      m_freeBlocks = std::move(other.m_freeBlocks);
      other.m_data = nullptr;
      other.m_size = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  ~RawBuffer() { if (m_data) alignedFree(m_data); }

  // 分配 size 字节（按 alignment 对齐），返回相对于 getBase() 的偏移量。
  [[nodiscard]] u32 allocate(u32 size, u32 alignment) {
    u32 alignedSize = (size + alignment - 1) & ~(alignment - 1);
    // First-fit 搜索空闲链表
    for (auto it = m_freeBlocks.begin(); it != m_freeBlocks.end(); ++it) {
      if (it->size >= alignedSize) {
        u32 off = it->offset;
        if (it->size > alignedSize) {
          it->offset += alignedSize;
          it->size -= alignedSize;
        } else {
          m_freeBlocks.erase(it);
        }
        return off;
      }
    }
    // 无可用块，扩容
    u32 off = (m_size + alignment - 1) & ~(alignment - 1);
    u32 needed = off + alignedSize;
    if (needed > m_capacity) {
      u32 newCap = std::max(m_capacity * 2, kInitialCapacity);
      while (newCap < needed) newCap *= 2;
      grow(newCap);
    }
    m_size = needed;
    return off;
  }

  // 释放之前 allocate 返回的块。自动合并相邻空闲块。
  void free(u32 offset, u32 size) {
    auto it = std::lower_bound(
        m_freeBlocks.begin(), m_freeBlocks.end(), offset,
        [](const FreeBlock &b, u32 o) { return b.offset < o; });
    FreeBlock block{offset, size};
    // 尝试与后一个块合并
    if (it != m_freeBlocks.end() && it->offset == offset + size) {
      block.size += it->size;
      it = m_freeBlocks.erase(it);
    }
    // 尝试与前一个块合并
    if (it != m_freeBlocks.begin()) {
      auto prev = std::prev(it);
      if (prev->offset + prev->size == offset) {
        prev->size += block.size;
        return;
      }
    }
    m_freeBlocks.insert(it, block);
  }

  // 返回内存块首地址（保证 kBaseAlignment = 256 字节对齐）。
  [[nodiscard]] u8 *getBase() { return m_data; }
  [[nodiscard]] const u8 *getBase() const { return m_data; }

  // 已使用的字节数。
  [[nodiscard]] u32 capacity() const { return m_size; }

  [[nodiscard]] u32 freeBlockCount() const { return (u32)m_freeBlocks.size(); }

  // 碎片率 = 空闲字节 / 已使用字节。> 30% 时建议 compact。
  [[nodiscard]] float fragmentationRatio() const {
    if (m_size == 0 || m_freeBlocks.empty()) return 0.0f;
    u32 totalFree = 0;
    for (auto &b : m_freeBlocks) totalFree += b.size;
    return (float)totalFree / (float)m_size;
  }
};

} // namespace LX_New_Common

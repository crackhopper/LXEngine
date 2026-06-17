module;
#include <algorithm>
#include <vector>

export module LX_New_Common.Memory:SpillBlock;

import LX_New_Common.Platform;

export namespace LX_New_Common {

// kNoneChunk — 链表尾哨兵值，表示"无下一块"。
// 同时存在于 LX_New_Core::kNoneChunk（定义在 resource/types.cppm 中）。
constexpr u32 kNoneChunk = 0xFFFFFFFF;

// SpillBlock<T, N> — 固定大小的溢出块。
// N 个 items + count + nextBlockIdx，通过 nextBlockIdx 形成单链表。
// 用途：当 inline 数组（4 个）溢出时，用 SpillPool 动态分配链式块。
template <typename T, u32 N = 8> struct SpillBlock {
  u8 count;        // 本块已用槽位数
  u32 nextBlockIdx; // 下一块索引，kNoneChunk = 链尾
  T items[N];
};

// SpillPool<T, N> — 溢出池管理器。
// 提供块分配、链释放、追加、遍历功能。
// 内部维护一个 vector 存储所有块 + freeList 复用已释放块。
//
// 典型用法（GameObjectManager 中管理超过 4 个的 ref 或 handle）：
//
//   SpillPool<u32, 8> pool;
//   u32 head = kNoneChunk;          // 初始空链
//   head = pool.append(head, 100);  // 追加第一个值
//   head = pool.append(head, 200);  // 追加第二个值
//
//   // 遍历链
//   for (auto it = pool.begin(head); it != pool.end(); ++it) {
//     process(*it);
//   }
//
//   // 释放整条链（归还到 freeList）
//   pool.freeChain(head);
//   head = kNoneChunk;
//
template <typename T, u32 N = 8> class SpillPool {
  std::vector<SpillBlock<T, N>> m_blocks;
  std::vector<u32> m_freeList;

public:
  // 分配一个空块，优先复用 freeList 中的块。
  u32 allocBlock() {
    if (!m_freeList.empty()) {
      u32 idx = m_freeList.back();
      m_freeList.pop_back();
      m_blocks[idx] = {0, kNoneChunk, {}};
      return idx;
    }
    u32 idx = (u32)m_blocks.size();
    m_blocks.push_back({0, kNoneChunk, {}});
    return idx;
  }

  // 释放整条链，所有块回收到 freeList。
  void freeChain(u32 headIdx) {
    u32 cur = headIdx;
    while (cur != kNoneChunk) {
      auto &block = m_blocks[cur];
      u32 next = block.nextBlockIdx;
      block.count = 0;
      block.nextBlockIdx = kNoneChunk;
      m_freeList.push_back(cur);
      cur = next;
    }
  }

  // 向链尾追加一个值，返回链头（首次追加时会自动分配块）。
  u32 append(u32 headIdx, T value) {
    if (headIdx == kNoneChunk) {
      headIdx = allocBlock();
    }
    u32 cur = headIdx;
    while (true) {
      if (m_blocks[cur].count < N) {
        m_blocks[cur].items[m_blocks[cur].count++] = value;
        return headIdx;
      }
      if (m_blocks[cur].nextBlockIdx == kNoneChunk) {
        m_blocks[cur].nextBlockIdx = allocBlock();
      }
      cur = m_blocks[cur].nextBlockIdx;
    }
  }

  // 统计链中元素总数。
  u32 itemCount(u32 headIdx) const {
    u32 count = 0;
    u32 cur = headIdx;
    while (cur != kNoneChunk) {
      count += m_blocks[cur].count;
      cur = m_blocks[cur].nextBlockIdx;
    }
    return count;
  }

  u32 freeListSize() const { return (u32)m_freeList.size(); }

  // 前向迭代器，跨链遍历所有 item。
  struct Iterator {
    const SpillPool *pool;
    u32 blockIdx;
    u8 itemIdx;
    const T &operator*() const {
      return pool->m_blocks[blockIdx].items[itemIdx];
    }
    void operator++() {
      auto &block = pool->m_blocks[blockIdx];
      itemIdx++;
      if (itemIdx >= block.count) {
        blockIdx = block.nextBlockIdx;
        itemIdx = 0;
      }
    }
    bool operator!=(const Iterator &o) const { return blockIdx != o.blockIdx; }
  };
  Iterator begin(u32 headIdx) const { return {this, headIdx, 0}; }
  Iterator end() const { return {this, kNoneChunk, 0}; }
};

} // namespace LX_New_Common

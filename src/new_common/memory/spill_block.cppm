module;
#include <vector>
#include <algorithm>

export module LX_New_Common.Memory:SpillBlock;

import LX_New_Common.Platform;

export namespace LX_New_Common {

// Inline sentinel to avoid circular dependency under MSVC
// (partition cannot import main module that re-exports it).
// Matches LX_New_Common.Memory.Types::kNoneChunk = 0xFFFFFFFF.
constexpr u32 kSpillNone = 0xFFFFFFFF;

template<typename T, u32 N = 8>
struct SpillBlock {
    u8  count;
    u32 nextBlockIdx;
    T   items[N];
};

template<typename T, u32 N = 8>
class SpillPool {
    std::vector<SpillBlock<T, N>> m_blocks;
    std::vector<u32>              m_freeList;

public:
    u32 allocBlock() {
        if (!m_freeList.empty()) {
            u32 idx = m_freeList.back();
            m_freeList.pop_back();
            m_blocks[idx] = {0, kSpillNone, {}};
            return idx;
        }
        u32 idx = (u32)m_blocks.size();
        m_blocks.push_back({0, kSpillNone, {}});
        return idx;
    }

    void freeChain(u32 headIdx) {
        u32 cur = headIdx;
        while (cur != kSpillNone) {
            auto& block = m_blocks[cur];
            u32 next = block.nextBlockIdx;
            block.count = 0;
            block.nextBlockIdx = kSpillNone;
            m_freeList.push_back(cur);
            cur = next;
        }
    }

    u32 append(u32 headIdx, T value) {
        if (headIdx == kSpillNone) {
            headIdx = allocBlock();
        }
        u32 cur = headIdx;
        while (true) {
            if (m_blocks[cur].count < N) {
                m_blocks[cur].items[m_blocks[cur].count++] = value;
                return headIdx;
            }
            if (m_blocks[cur].nextBlockIdx == kSpillNone) {
                m_blocks[cur].nextBlockIdx = allocBlock();
            }
            cur = m_blocks[cur].nextBlockIdx;
        }
    }

    u32 itemCount(u32 headIdx) const {
        u32 count = 0;
        u32 cur = headIdx;
        while (cur != kSpillNone) {
            count += m_blocks[cur].count;
            cur = m_blocks[cur].nextBlockIdx;
        }
        return count;
    }

    u32 freeListSize() const { return (u32)m_freeList.size(); }

    struct Iterator {
        const SpillPool* pool;
        u32 blockIdx;
        u8  itemIdx;
        const T& operator*() const { return pool->m_blocks[blockIdx].items[itemIdx]; }
        void operator++() {
            auto& block = pool->m_blocks[blockIdx];
            itemIdx++;
            if (itemIdx >= block.count) {
                blockIdx = block.nextBlockIdx;
                itemIdx = 0;
            }
        }
        bool operator!=(const Iterator& o) const { return blockIdx != o.blockIdx; }
    };
    Iterator begin(u32 headIdx) const { return {this, headIdx, 0}; }
    Iterator end() const { return {this, kSpillNone, 0}; }
};

} // namespace LX_New_Common

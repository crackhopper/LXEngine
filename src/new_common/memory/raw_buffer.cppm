module;
#include <algorithm>
#include <vector>

export module LX_New_Common.Memory:RawBuffer;

import LX_New_Common.Platform;

export namespace LX_New_Common {

class RawBuffer {
    std::vector<u8> m_data;

    struct FreeBlock {
        u32 offset;
        u32 size;
    };
    std::vector<FreeBlock> m_freeBlocks;

    static constexpr u32 kInitialCapacity = 1024 * 1024; // 1MB

public:
    [[nodiscard]] u32 allocate(u32 size, u32 alignment) {
        u32 alignedSize = (size + alignment - 1) & ~(alignment - 1);
        // First-fit search
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
        // Expand
        u32 off = (u32)m_data.size();
        u32 alignedOff = (off + alignment - 1) & ~(alignment - 1);
        u32 newSize = alignedOff + alignedSize;
        u32 cap = m_data.capacity();
        if (newSize > cap) {
            u32 newCap = std::max(cap * 2, kInitialCapacity);
            while (newCap < newSize) newCap *= 2;
            m_data.reserve(newCap);
        }
        m_data.resize(newSize);
        return alignedOff;
    }

    void free(u32 offset, u32 size) {
        auto it = std::lower_bound(m_freeBlocks.begin(), m_freeBlocks.end(), offset,
            [](const FreeBlock& b, u32 o) { return b.offset < o; });
        FreeBlock block{offset, size};
        // Try merge with next
        if (it != m_freeBlocks.end() && it->offset == offset + size) {
            block.size += it->size;
            it = m_freeBlocks.erase(it);
        }
        // Try merge with prev
        if (it != m_freeBlocks.begin()) {
            auto prev = std::prev(it);
            if (prev->offset + prev->size == offset) {
                prev->size += block.size;
                return;
            }
        }
        m_freeBlocks.insert(it, block);
    }

    [[nodiscard]] u8* getBase() { return m_data.data(); }
    [[nodiscard]] const u8* getBase() const { return m_data.data(); }
    [[nodiscard]] u32 capacity() const { return (u32)m_data.size(); }
    [[nodiscard]] u32 freeBlockCount() const { return (u32)m_freeBlocks.size(); }

    [[nodiscard]] float fragmentationRatio() const {
        if (m_data.empty() || m_freeBlocks.empty()) return 0.0f;
        u32 totalFree = 0;
        for (auto& b : m_freeBlocks) totalFree += b.size;
        return (float)totalFree / (float)m_data.size();
    }
};

} // namespace LX_New_Common

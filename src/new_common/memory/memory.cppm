module;
#include <algorithm>
#include <vector>
#include <cstring>
#include <span>

export module LX_New_Common.Memory;

export import :Types;
export import :ResourceHandle;
export import :RawBuffer;
export import :SpillBlock;

export namespace LX_New_Common {

template<typename T>
class TypedResourceTable {
    struct Meta {
        u16 refCount;
        u16 generation;
    };

    std::vector<Meta> m_metas;
    std::vector<T>    m_data;
    std::vector<u32>  m_freeList;
    u8                m_typeId = 0;

public:
    void setTypeId(u8 typeId) { m_typeId = typeId; }

    ResourceHandle allocate(const T& dataItem) {
        u32 idx;
        if (!m_freeList.empty()) {
            idx = m_freeList.back();
            m_freeList.pop_back();
            m_metas[idx].refCount = 1;
            m_metas[idx].generation++;
            m_data[idx] = dataItem;
        } else {
            idx = (u32)m_metas.size();
            m_metas.push_back({1, 1});
            m_data.push_back(dataItem);
        }
        ResourceHandle h{};
        h.type_id = m_typeId;
        h.index = idx;
        h.generation = m_metas[idx].generation;
        return h;
    }

    void release(ResourceHandle handle) {
        if (!handle.isValid()) return;
        u32 idx = handle.index;
        if (idx >= m_metas.size()) return;
        if (m_metas[idx].generation != handle.generation) return;
        if (m_metas[idx].refCount > 0) {
            m_metas[idx].refCount--;
        }
        if (m_metas[idx].refCount == 0) {
            m_freeList.push_back(idx);
        }
    }

    T* get(ResourceHandle handle) {
        if (!handle.isValid()) return nullptr;
        u32 idx = handle.index;
        if (idx >= m_metas.size()) return nullptr;
        if (m_metas[idx].generation != handle.generation) return nullptr;
        if (m_metas[idx].refCount == 0) return nullptr;
        return &m_data[idx];
    }
};

template<typename MetaType>
class VariableResourceTable {
    struct Entry {
        MetaType meta;
        u16 refCount;
        u16 generation;
        u32 rawOffset;
        u32 rawSize;
    };

    std::vector<Entry> m_entries;
    std::vector<u32>   m_freeList;
    RawBuffer          m_rawBuffer;
    u8                 m_typeId = 0;

public:
    void setTypeId(u8 typeId) { m_typeId = typeId; }

    [[nodiscard]] ResourceHandle allocate(const MetaType& meta, std::span<const u8> rawData) {
        u32 idx;
        if (!m_freeList.empty()) {
            idx = m_freeList.back();
            m_freeList.pop_back();
            m_entries[idx].meta = meta;
            m_entries[idx].refCount = 1;
            m_entries[idx].generation++;
        } else {
            idx = (u32)m_entries.size();
            m_entries.push_back({});
            auto& e = m_entries.back();
            e.meta = meta;
            e.refCount = 1;
            e.generation = 1;
        }
        Entry& e = m_entries[idx];
        e.rawOffset = m_rawBuffer.allocate((u32)rawData.size(), 16);
        e.rawSize = (u32)rawData.size();
        std::memcpy(m_rawBuffer.getBase() + e.rawOffset, rawData.data(), rawData.size());

        ResourceHandle h{};
        h.type_id = m_typeId;
        h.index = idx;
        h.generation = e.generation;
        return h;
    }

    void release(ResourceHandle handle) {
        if (!handle.isValid()) return;
        u32 idx = handle.index;
        if (idx >= m_entries.size()) return;
        auto& e = m_entries[idx];
        if (e.generation != handle.generation) return;
        if (e.refCount > 0) e.refCount--;
        if (e.refCount == 0) {
            m_rawBuffer.free(e.rawOffset, e.rawSize);
            m_freeList.push_back(idx);
        }
    }

    [[nodiscard]] MetaType* getMeta(ResourceHandle handle) {
        if (!handle.isValid()) return nullptr;
        u32 idx = handle.index;
        if (idx >= m_entries.size()) return nullptr;
        if (m_entries[idx].generation != handle.generation) return nullptr;
        if (m_entries[idx].refCount == 0) return nullptr;
        return &m_entries[idx].meta;
    }

    [[nodiscard]] u8* getRawData(ResourceHandle handle) {
        if (!handle.isValid()) return nullptr;
        u32 idx = handle.index;
        if (idx >= m_entries.size()) return nullptr;
        auto& e = m_entries[idx];
        if (e.generation != handle.generation) return nullptr;
        if (e.refCount == 0) return nullptr;
        return m_rawBuffer.getBase() + e.rawOffset;
    }
};

// ---------------------------------------------------------------------------
// GameObjectMeta (56 bytes) + GameObjectManager (mark-sweep GC from Root)
// Inlined into this TU because MSVC cannot resolve partition-to-partition
// imports under FILE_SET CXX_MODULES.
// ---------------------------------------------------------------------------

export constexpr u32 kInlineRefCount = 4;
export constexpr u32 kInlineHandleCount = 4;

export struct GameObjectMeta {
    u8  marked : 1;
    u8  alive  : 1;
    u8  _pad   : 6;

    u8  refCount;
    u32 refs[kInlineRefCount];
    u32 refSpillHead;

    u8  handleCount;
    u64 handles[kInlineHandleCount];
    u32 handleSpillHead;
};

export class GameObjectManager {
    std::vector<GameObjectMeta> m_metas;
    std::vector<u32>            m_freeMetaList;
    std::vector<u32>            m_roots;
    SpillPool<u32, 8>  m_refPool;
    SpillPool<u64, 8>  m_handlePool;

public:
    u32 allocMeta() {
        if (!m_freeMetaList.empty()) {
            u32 idx = m_freeMetaList.back();
            m_freeMetaList.pop_back();
            m_metas[idx] = GameObjectMeta{};
            m_metas[idx].alive = true;
            return idx;
        }
        u32 idx = (u32)m_metas.size();
        m_metas.push_back(GameObjectMeta{});
        m_metas[idx].alive = true;
        return idx;
    }

    void freeMeta(u32 idx) {
        m_metas[idx].alive = false;
        m_freeMetaList.push_back(idx);
    }

    bool isAlive(u32 idx) const { return idx < m_metas.size() && m_metas[idx].alive; }

    void setRefs(u32 metaIdx, std::span<const u32> targets) {
        auto& meta = m_metas[metaIdx];
        meta.refCount = 0;
        meta.refSpillHead = kNoneChunk;
        for (u32 t : targets) {
            if (meta.refCount < kInlineRefCount) {
                meta.refs[meta.refCount++] = t;
            } else {
                meta.refSpillHead = m_refPool.append(meta.refSpillHead, t);
            }
        }
    }

    u8 getRefCount(u32 idx) const { return m_metas[idx].refCount; }
    u32 getSpillRefCount(u32 idx) const { return m_refPool.itemCount(m_metas[idx].refSpillHead); }

    void setHandles(u32 metaIdx, std::span<const u64> handles) {
        auto& meta = m_metas[metaIdx];
        meta.handleCount = 0;
        meta.handleSpillHead = kNoneChunk;
        for (u64 h : handles) {
            if (meta.handleCount < kInlineHandleCount) {
                meta.handles[meta.handleCount++] = h;
            } else {
                meta.handleSpillHead = m_handlePool.append(meta.handleSpillHead, h);
            }
        }
    }

    u8 getHandleCount(u32 idx) const { return m_metas[idx].handleCount; }
    u32 getSpillHandleCount(u32 idx) const { return m_handlePool.itemCount(m_metas[idx].handleSpillHead); }

    void addToRoot(u32 idx) { m_roots.push_back(idx); }
    void removeFromRoot(u32 idx) {
        auto it = std::find(m_roots.begin(), m_roots.end(), idx);
        if (it != m_roots.end()) { *it = m_roots.back(); m_roots.pop_back(); }
    }
    u32 getRootCount() const { return (u32)m_roots.size(); }

    void mark() {
        for (auto& meta : m_metas) { if (meta.alive) meta.marked = false; }
        for (u32 r : m_roots) markRecursive(r);
    }

    void clearMarks() {
        for (auto& meta : m_metas) meta.marked = false;
    }

    bool isMarked(u32 idx) const { return idx < m_metas.size() && m_metas[idx].marked; }

    void markRecursive(u32 metaIdx) {
        if (metaIdx >= m_metas.size()) return;
        auto& meta = m_metas[metaIdx];
        if (!meta.alive || meta.marked) return;
        meta.marked = true;
        for (u8 i = 0; i < meta.refCount; ++i) markRecursive(meta.refs[i]);
        for (auto it = m_refPool.begin(meta.refSpillHead); it != m_refPool.end(); ++it)
            markRecursive(*it);
    }

    void sweep() {
        for (u32 i = 0; i < m_metas.size(); ++i) {
            auto& meta = m_metas[i];
            if (!meta.alive || meta.marked) continue;
            meta.alive = false;
            meta.marked = false;
            meta.refCount = 0;
            meta.handleCount = 0;
            if (meta.refSpillHead != kNoneChunk) { m_refPool.freeChain(meta.refSpillHead); meta.refSpillHead = kNoneChunk; }
            if (meta.handleSpillHead != kNoneChunk) { m_handlePool.freeChain(meta.handleSpillHead); meta.handleSpillHead = kNoneChunk; }
            m_freeMetaList.push_back(i);
        }
    }

    void tick() { mark(); sweep(); }
};

} // namespace LX_New_Common

module;
#include <algorithm>
#include <vector>
#include <cstring>
#include <span>

export module LX_New_Common.Memory;

export import :Types;
export import :ResourceHandle;
export import :RawBuffer;

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

} // namespace LX_New_Common

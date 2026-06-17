module;
#include <algorithm>
#include <vector>

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

} // namespace LX_New_Common

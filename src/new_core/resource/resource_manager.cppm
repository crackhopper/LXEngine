module;
#include <compare>

export module LX_New_Core.Resource:ResourceManager;

import :Types;
import :ResourceHandle;
import :TypedResourceTable;
import :VariableResourceTable;

export namespace LX_New_Core {

class ResourceManager {
  ResourceTypeTableBase *m_tables[256] = {};

public:
  ResourceManager() = default;

  ResourceManager(const ResourceManager &) = delete;
  ResourceManager &operator=(const ResourceManager &) = delete;

  void registerTable(u8 typeId, ResourceTypeTableBase *table) {
    m_tables[typeId] = table;
  }

  void release(ResourceHandle handle) {
    if (!handle.isValid())
      return;
    u8 typeId = handle.type_id();
    if (m_tables[typeId])
      m_tables[typeId]->release(handle.raw);
  }

  void release(u64 rawHandle) { release(ResourceHandle{rawHandle}); }
};

} // namespace LX_New_Core

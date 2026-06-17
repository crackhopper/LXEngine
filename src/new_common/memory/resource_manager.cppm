module;
#include <memory>

export module LX_New_Common.Memory:ResourceManager;

import LX_New_Common.Memory;

export namespace LX_New_Common {

// ResourceManager: thin dispatch layer over typed resource tables.
// For now, serves as a facade. Actual typed table access is done directly
// via TypedResourceTable<T> and VariableResourceTable<MetaType>.
// The full dispatch-by-type_id implementation requires runtime type registration
// which will be added when concrete resource types (VertexBuffer, TextureBuffer, etc.)
// are defined.
class ResourceManager {
public:
    ResourceManager() = default;
};

} // namespace LX_New_Common

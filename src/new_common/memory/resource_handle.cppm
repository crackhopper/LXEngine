module;
#include <cstdint>

export module LX_New_Common.Memory:ResourceHandle;

import LX_New_Common.Platform;

export namespace LX_New_Common {

// Forward type aliases (defined in :Types partition); needed here so
// ResourceHandle does not depend on partition-to-partition imports,
// which MSVC's FILE_SET CXX_MODULES does not resolve correctly.
using ResourceIndex = u32;
using ResourceGeneration = u16;
using ResourceTypeId = u8;

union ResourceHandle {
    u64 raw;
    struct {
        u64 type_id    : 8;
        u64 index      : 24;
        u64 generation : 16;
        u64 _reserved  : 16;
    };

    [[nodiscard]] bool isValid() const { return generation != 0; }
    [[nodiscard]] static ResourceHandle invalid() { ResourceHandle h{}; return h; }
};

} // namespace LX_New_Common

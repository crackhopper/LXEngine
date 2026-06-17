---
name: msvc-cpp20-module-partition-imports
description: MSVC C++20 module partition import syntax rules — use short form :PartitionName for same-module imports, full name for cross-module
source: auto-skill
extracted_at: '2026-06-17T16:16:03.962Z'
---

# MSVC C++20 Module Partition Import Syntax

## Critical Rule

When importing partitions within the **same module**, always use the **short form**:

```cpp
// ✅ CORRECT — same module partition
import :Types;
import :ResourceHandle;
```

**Never** use the full module name for same-module partitions:

```cpp
// ❌ WRONG — MSVC CXX_SCAN_FOR_MODULES cannot resolve this
import LX_New_Core.Resource.Types;
import LX_New_Core.Resource.ResourceHandle;
```

## Cross-Module Imports

When importing from a **different module**, import the **parent module** by its full name:

```cpp
// ✅ CORRECT — different module, import parent
import LX_New_Common.Platform;
import LX_New_Common.Memory;      // gets RawBuffer + SpillBlock via export import
```

**Never** import a partition by its dotted name from outside the module — MSVC rejects it:

```cpp
// ❌ WRONG — partition dotted name from another module
import LX_New_Common.Memory.RawBuffer;
// → error C2230: 无法找到模块"LX_New_Common.Memory.RawBuffer"
```

Partitions are only addressable via `:Name` from within the same module, or re-exported through the parent's `export import :PartitionName;`. To consume a partition's types from another module, import the parent module and read its `export import` declarations.

## Why This Matters

MSVC's `FILE_SET CXX_MODULES` with `CXX_SCAN_FOR_MODULES` treats `import Module:Partition` as a cross-module dependency, not a same-module partition reference. This causes:
- `error C2230: 无法找到模块` — module not found
- `error C2061: 语法错误: 标识符"u32"` — types from the partition are invisible
- Cascading errors throughout all files that depend on the broken partition

## Module Structure Pattern

```cpp
// partition file: types.cppm
export module LX_New_Core.Resource:Types;
import LX_New_Common.Platform;  // ← different module, full name

// partition file: resource_handle.cppm
export module LX_New_Core.Resource:ResourceHandle;
import :Types;  // ← same module, SHORT form only

// umbrella file: resource.cppm
export module LX_New_Core.Resource;
export import :Types;
export import :ResourceHandle;
```

## CMake Requirements

Both source and consumer targets need `CXX_SCAN_FOR_MODULES ON`:

```cmake
set_target_properties(MyLib PROPERTIES CXX_SCAN_FOR_MODULES ON)
```

## Troubleshooting

| Symptom | Likely Cause |
|---|---|
| `error C2230: 无法找到模块"LX_..."` | Used dotted partition name for cross-module import — import parent instead, OR used full module name for same-module partition |
| `error C2061: 语法错误: 标识符"u32"` | Partition import failed (types invisible) **or** namespace-scoped type alias used unqualified (see Namespace Visibility below) |
| `error C2039: "RawBuffer": 不是 "LX_New_Common" 的成员` | Tried to import a partition directly; parent's `export import` not seen |
| `error C3774: 找不到"std::partial_ordering"` | Module exports `operator<=>`; importer lacks `<compare>` (see STL Headers below) |
| `error C3474: 无法打开输出文件"...ifc"` | Stale BMI files — delete build dir and reconfigure |

**When partition imports fail after fixing syntax:** clean the build directory completely and re-run CMake configure. MSVC caches BMI dependencies aggressively.

## Namespace Visibility for Exported Types

Types exported inside a namespace (e.g. `u32`, `u64` defined in `namespace LX_New_Common`) are only accessible **qualified** from outside that namespace:

```cpp
// provider module: new_common/platform/types.cppm
export namespace LX_New_Common {
  using u32 = uint32_t;
  using u64 = uint64_t;
}

// consumer module: new_core — type is NOT in global scope after import
export namespace LX_New_Core {
  u64 raw = 0;               // ❌ C2061: unknown identifier "u64"
  LX_New_Common::u64 raw = 0; // ✅ qualified works
}
```

To use them unqualified, add a `using namespace` directive at the top of your own namespace in **each module unit** that needs the aliases. The directive is NOT exported, so each file needs its own:

```cpp
export namespace LX_New_Core {
  using namespace LX_New_Common;  // ← brings u8/u16/u32/u64 into scope
  using MemoryIndex = u32;        // ✅ now visible
}
```

## STL Headers Don't Propagate Across Module Imports

The **global module fragment** includes (`#include <compare>` etc.) are **not** visible to importers — they only serve the current translation unit. If an exported type uses a feature that depends on a standard library header, every importing module unit must `#include` it itself.

Most common case: a `default`ed `operator<=>` requires `<compare>` for its return type (`std::partial_ordering`, `std::strong_ordering`, etc.):

```cpp
// provider: resource_handle.cppm — has <compare> in global fragment ✓
export struct ResourceHandle {
  ...
  auto operator<=>(const ResourceHandle&) const = default;  // needs <compare>
};

// consumer WITHOUT <compare>:
export module LX_New_Core.Resource:ResourceManager;  // ❌ C3774
import :ResourceHandle;  // operator<=> return type unknown

// consumer WITH <compare>:
module;
#include <compare>       // ← importer must include it itself ✓
export module LX_New_Core.Resource:ResourceManager;
import :ResourceHandle;
```

This affects **both** partition importers and umbrella modules that `export import` the partition. The umbrella `resource.cppm` also needs `#include <compare>` even though it only re-exports.

### Quick Checklist for Cross-Module Type Consumption

1. Import the **parent** module, not the partition by dotted name
2. Add `using namespace LX_New_Common;` if using namespace-scoped aliases unqualified
3. `#include` any STL header the exported type's interface depends on (`<compare>` for `operator<=>`, `<iterator>`/`<ranges>` for view types, etc.)

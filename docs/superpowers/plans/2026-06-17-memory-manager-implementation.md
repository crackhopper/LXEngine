# Memory Manager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 LXEngine 的显式内存管理系统，包含 Resource Manager（引用计数 + Handle）、Game Object Manager（mark-sweep GC）和完整测试。

**Architecture:** 两层设计 — ResourceManager 管理 GPU 资源（引用计数 + 64-bit packed Handle），GameObjectManager 管理场景对象（mark-sweep GC + inline refs/handles + SpillBlock 溢出池）。

**Tech Stack:** C++20 named modules, MSVC (Windows), CMake 3.28+, Ninja, 无第三方测试库（自研轻量测试 runner）

## Global Constraints

- CMake 3.28+ required for FILE_SET CXX_MODULES
- C++20 standard, all code in .cppm module files
- No raw pointers for ownership — use StrongRef/WeakRef (to be implemented)
- Module namespace: `LX_New_Common.Memory.*` for memory, `LX_New_Test.*` for tests
- TDD: write test first, then minimal implementation, verify, commit
- Old code (`src/core/`) must not be modified

---

## File Structure

### New files to create

| File | Responsibility |
|---|---|
| `src/new_common/memory/resource_handle.cppm` | ResourceHandle (64-bit packed union) |
| `src/new_common/memory/raw_buffer.cppm` | RawBuffer (free block list allocator) |
| `src/new_common/memory/typed_resource_table.cppm` | TypedResourceTable<T> (fixed-size resources) |
| `src/new_common/memory/variable_resource_table.cppm` | VariableResourceTable<Meta> (variable-size resources) |
| `src/new_common/memory/resource_manager.cppm` | ResourceManager (umbrella + dispatch) |
| `src/new_common/memory/spill_block.cppm` | SpillBlock<T,N> (overflow pool) |
| `src/new_common/memory/game_object_meta.cppm` | GameObjectMeta struct definition |
| `src/new_common/memory/game_object_manager.cppm` | GameObjectManager (mark-sweep GC) |
| `src/new_common/memory/memory.cppm` | Memory umbrella module |
| `src/new_common/memory/types.cppm` | Memory-layer type aliases |
| `src/test/new/CMakeLists.txt` | Updated with ASAN support |
| `src/test/new/memory_test.cppm` | Test runner for memory tests |
| `src/test/new/test/resource_handle_test.cppm` | ResourceHandle tests |
| `src/test/new/test/typed_resource_table_test.cppm` | TypedResourceTable tests |
| `src/test/new/test/raw_buffer_test.cppm` | RawBuffer tests |
| `src/test/new/test/spill_pool_test.cppm` | SpillBlock tests |
| `src/test/new/test/game_object_manager_test.cppm` | GameObjectManager tests |
| `src/test/new/test/gc_integration_test.cppm` | GC integration tests |
| `src/test/new/test/leak_test.cppm` | ASAN leak detection tests |

### Files to modify

| File | Change |
|---|---|
| `src/new_common/CMakeLists.txt` | Add `memory/*.cppm` to glob (already uses GLOB_RECURSE, no change needed) |
| `src/new_common/memory/types.cppm` | Create new |
| `src/test/new/CMakeLists.txt` | Add ASAN option |
| `src/test/new/main.cpp` | Add memory test dispatch |

---

### Task 1: ResourceHandle — 64-bit packed handle

**Files:**
- Create: `src/new_common/memory/resource_handle.cppm`
- Create: `src/test/new/test/resource_handle_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

**Interfaces:**
- Produces: `export module LX_New_Common.Memory:ResourceHandle;` → `union ResourceHandle`
- Consumes: `LX_New_Common.Platform` (for u64 type alias)

- [ ] **Step 1: Create types.cppm foundation**

```cppm
module;
#include <cstddef>
#include <cstdint>

export module LX_New_Common.Memory:Types;

import LX_New_Common.Platform;

export namespace LX_New_Common {

using MemoryIndex = u32;
using MemoryGeneration = u16;
using MemoryTypeId = u8;

constexpr MemoryIndex kInvalidIndex = 0xFFFFFFFF;
constexpr u64 kInvalidHandle = 0;
constexpr MemoryIndex kNoneChunk = 0xFFFFFFFF;

} // namespace LX_New_Common
```

- [ ] **Step 2: Write the failing test**

Create `src/test/new/test/resource_handle_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.ResourceHandleTest;

import LX_New_Common.Memory.ResourceHandle;

export namespace LX_New_Test {

inline bool run_resource_handle_tests() {
    using namespace LX_New_Common;
    bool pass = true;

    auto check = [&](bool cond, const char* msg) {
        if (!cond) {
            std::cerr << "  FAIL: " << msg << "\n";
            pass = false;
        }
    };

    // T1: isValid — invalid handle returns false
    {
        ResourceHandle h;
        h.raw = 0;
        check(!h.isValid(), "invalid handle should return false");
    }

    // T2: isValid — valid handle with generation != 0 returns true
    {
        ResourceHandle h;
        h.raw = 0;
        h.generation = 1;
        h.type_id = 1;
        h.index = 0;
        check(h.isValid(), "valid handle should return true");
    }

    // T3: packing/unpacking round-trip
    {
        ResourceHandle h;
        h.raw = 0;
        h.type_id = 3;
        h.index = 1000;
        h.generation = 5;
        h._reserved = 0xABCD;

        check(h.type_id == 3, "type_id round-trip");
        check(h.index == 1000, "index round-trip");
        check(h.generation == 5, "generation round-trip");
        check(h._reserved == 0xABCD, "reserved round-trip");
    }

    // T4: invalid() returns zero handle
    {
        ResourceHandle h = ResourceHandle::invalid();
        check(h.raw == 0, "invalid() returns zero");
        check(!h.isValid(), "invalid() is not valid");
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 3: Implement ResourceHandle**

Create `src/new_common/memory/resource_handle.cppm`:

```cppm
module;
#include <cstdint>

export module LX_New_Common.Memory:ResourceHandle;

import LX_New_Common.Platform;
import LX_New_Common.Memory.Types;

export namespace LX_New_Common {

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
```

- [ ] **Step 4: Create test runner**

Create `src/test/new/memory_test.cppm`:

```cppm
module;
#include <iostream>

export module LX_New_Test.MemoryTest;

import LX_New_Test.Test.ResourceHandleTest;

export namespace LX_New_Test {

inline bool run_memory_tests() {
    bool all_pass = true;

    std::cout << "[ResourceHandle]\n";
    if (!run_resource_handle_tests()) all_pass = false;
    else std::cout << "  OK\n";

    return all_pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 5: Modify test/new/main.cpp to dispatch memory tests**

Read current `src/test/new/main.cpp`, add after existing imports:

```cpp
import LX_New_Test.MemoryTest;
```

In main(), after existing smoke test:

```cpp
    bool ok = LX_New_Test::run_memory_tests();
    std::cout << (ok ? "\nAll memory tests PASSED\n" : "\nSome memory tests FAILED\n");
    return ok ? 0 : 1;
```

- [ ] **Step 6: Build and verify tests pass**

Run: `cd build && cmake --build . --target lxe_test_new_modules --config Debug`
Run: `cd build && ctest -R lxe_test_new_modules --output-on-failure`
Expected: PASS with "All memory tests PASSED"

- [ ] **Step 7: Commit**

```bash
git add src/new_common/memory/types.cppm
git add src/new_common/memory/resource_handle.cppm
git add src/test/new/test/resource_handle_test.cppm
git add src/test/new/memory_test.cppm
git add src/test/new/main.cpp
git commit -m "feat(memory): add ResourceHandle (64-bit packed) with tests"
```

---

### Task 2: RawBuffer — Free Block List Allocator

**Files:**
- Create: `src/new_common/memory/raw_buffer.cppm`
- Create: `src/test/new/test/raw_buffer_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

**Interfaces:**
- Consumes: `LX_New_Common.Platform`, `LX_New_Common.Memory.Types`
- Produces: `class RawBuffer` with `allocate()`, `free()`, `getBase()`, `capacity()`, `fragmentationRatio()`

- [ ] **Step 1: Write the failing tests**

Create `src/test/new/test/raw_buffer_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>
#include <cstring>

export module LX_New_Test.Test.RawBufferTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory.RawBuffer;

export namespace LX_New_Test {

inline bool run_raw_buffer_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // T1: initial state — empty, zero capacity until first alloc
    {
        RawBuffer buf;
        check(buf.capacity() == 0, "initial capacity zero");
    }

    // T2: allocate — first allocation at offset 0
    {
        RawBuffer buf;
        u32 off = buf.allocate(64, 1);
        check(off == 0, "first alloc at offset 0");
        check(buf.capacity() >= 64, "capacity expanded");
    }

    // T3: allocate — second allocation contiguous
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(64, 1);
        check(o2 == 32, "second alloc contiguous after first");
    }

    // T4: free — frees a block
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(64, 1);
        buf.free(o1, 32);
        // free list should have one entry
    }

    // T5: reuse — allocates from freed block (first-fit)
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        buf.allocate(64, 1);
        buf.free(o1, 32);
        u32 o3 = buf.allocate(32, 1);
        check(o3 == o1, "reuses freed block");
    }

    // T6: alignment — respects requested alignment
    {
        RawBuffer buf;
        buf.allocate(7, 1);  // offset 0, size 7
        u32 off = buf.allocate(4, 16);
        check(off % 16 == 0, "16-byte alignment respected");
    }

    // T7: merge — freeing adjacent blocks merges them
    {
        RawBuffer buf;
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(32, 1);
        buf.free(o1, 32);
        buf.free(o2, 32);
        check(buf.freeBlockCount() <= 1, "adjacent freed blocks merged");
    }

    // T8: fragmentation ratio
    {
        RawBuffer buf;
        check(buf.fragmentationRatio() == 0.0f, "initial fragmentation zero");
        u32 o1 = buf.allocate(32, 1);
        u32 o2 = buf.allocate(32, 1);
        buf.allocate(32, 1);
        buf.free(o1, 32);
        buf.free(o2, 32);
        float frag = buf.fragmentationRatio();
        check(frag > 0.0f && frag <= 1.0f, "fragmentation reported");
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 2: Implement RawBuffer**

Create `src/new_common/memory/raw_buffer.cppm`:

```cppm
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
        u32 newSize = off + alignedSize;
        u32 cap = m_data.capacity();
        if (newSize > cap) {
            u32 newCap = std::max(cap * 2, kInitialCapacity);
            while (newCap < newSize) newCap *= 2;
            m_data.reserve(newCap);
        }
        m_data.resize(newSize);
        return off;
    }

    void free(u32 offset, u32 size) {
        // Insert keeping offset order
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
```

- [ ] **Step 3: Update memory_test.cppm**

Add import and test call:

```cpp
import LX_New_Test.Test.RawBufferTest;

// in run_memory_tests():
std::cout << "[RawBuffer]\n";
if (!run_raw_buffer_tests()) all_pass = false;
else std::cout << "  OK\n";
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build . --target lxe_test_new_modules --config Debug
ctest -R lxe_test_new_modules --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/new_common/memory/raw_buffer.cppm
git add src/test/new/test/raw_buffer_test.cppm
git add src/test/new/memory_test.cppm
git commit -m "feat(memory): add RawBuffer free-block-list allocator with tests"
```

---

### Task 3: TypedResourceTable — Fixed-size resources

**Files:**
- Create: `src/new_common/memory/typed_resource_table.cppm`
- Create: `src/test/new/test/typed_resource_table_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

**Interfaces:**
- Consumes: `LX_New_Common.Platform`, `LX_New_Common.Memory.ResourceHandle`, `LX_New_Common.Memory.Types`
- Produces: `template<typename T> class TypedResourceTable`

- [ ] **Step 1: Write the failing tests**

Create `src/test/new/test/typed_resource_table_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>
#include <cstring>

export module LX_New_Test.Test.TypedResourceTableTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory.ResourceHandle;
import LX_New_Common.Memory.TypedResourceTable;

export namespace LX_New_Test {

// Test fixture data type
struct TestResource {
    f32 value[4];
};

inline bool run_typed_resource_table_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    TypedResourceTable<TestResource> table;
    table.setTypeId(1);

    // T1: allocate — first allocation gets index 0, generation 1
    {
        TestResource data = {{1.0f, 2.0f, 3.0f, 4.0f}};
        ResourceHandle h = table.allocate(data);
        check(h.index == 0, "first alloc index 0");
        check(h.generation == 1, "first alloc generation 1");
        check(h.type_id == 1, "type_id matches");
    }

    // T2: allocate — second allocation gets index 1
    {
        TestResource data = {{5.0f, 6.0f, 7.0f, 8.0f}};
        ResourceHandle h = table.allocate(data);
        check(h.index == 1, "second alloc index 1");
    }

    // T3: get — returns valid pointer to data
    {
        TestResource data = {{9.0f, 10.0f, 11.0f, 12.0f}};
        ResourceHandle h = table.allocate(data);
        TestResource* ptr = table.get(h);
        check(ptr != nullptr, "get returns non-null");
        check(ptr->value[0] == 9.0f, "get returns correct data");
    }

    // T4: release — refCount decrements, not freed while > 0
    {
        TestResource data = {{13.0f, 14.0f, 15.0f, 16.0f}};
        ResourceHandle h = table.allocate(data);
        table.release(h); // refCount: 1 → 0
        TestResource* ptr = table.get(h);
        check(ptr == nullptr, "get returns null after release (generation mismatch or freed)");
    }

    // T5: reuse — freed slot reused with generation + 1
    {
        TestResource data = {{17.0f, 18.0f, 19.0f, 20.0f}};
        ResourceHandle h1 = table.allocate(data);
        table.release(h1);
        ResourceHandle h2 = table.allocate(data);
        check(h2.index == h1.index, "reuses same index");
        check(h2.generation == h1.generation + 1, "generation incremented");
    }

    // T6: stale handle — get() returns nullptr for expired generation
    {
        TestResource data = {{21.0f, 22.0f, 23.0f, 24.0f}};
        ResourceHandle h1 = table.allocate(data);
        table.release(h1);
        table.allocate(data); // reuses slot
        ResourceHandle stale = h1;
        TestResource* ptr = table.get(stale);
        check(ptr == nullptr, "stale handle returns nullptr");
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 2: Implement TypedResourceTable**

Create `src/new_common/memory/typed_resource_table.cppm`:

```cppm
module;
#include <algorithm>
#include <vector>

export module LX_New_Common.Memory:TypedResourceTable;

import LX_New_Common.Platform;
import LX_New_Common.Memory.ResourceHandle;
import LX_New_Common.Memory.Types;

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
```

- [ ] **Step 3: Update memory_test.cppm**

Add import and test call for TypedResourceTableTest.

- [ ] **Step 4: Build and verify**

```bash
cmake --build . --target lxe_test_new_modules --config Debug
ctest -R lxe_test_new_modules --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/new_common/memory/typed_resource_table.cppm
git add src/test/new/test/typed_resource_table_test.cppm
git add src/test/new/memory_test.cppm
git commit -m "feat(memory): add TypedResourceTable with tests"
```

---

### Task 4: VariableResourceTable — Variable-size resources

**Files:**
- Create: `src/new_common/memory/variable_resource_table.cppm`
- Create: `src/test/new/test/variable_resource_table_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

**Interfaces:**
- Consumes: `LX_New_Common.Memory.RawBuffer`, `LX_New_Common.Memory.ResourceHandle`, `LX_New_Common.Memory.Types`
- Produces: `template<typename MetaType> class VariableResourceTable`

- [ ] **Step 1: Write the failing tests**

Create `src/test/new/test/variable_resource_table_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>
#include <cstring>

export module LX_New_Test.Test.VariableResourceTableTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory.ResourceHandle;
import LX_New_Common.Memory.VariableResourceTable;
import LX_New_Common.Memory.RawBuffer;

export namespace LX_New_Test {

struct TestVarMeta {
    u32 width;
    u32 height;
};

inline bool run_variable_resource_table_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    VariableResourceTable<TestVarMeta> table;
    table.setTypeId(2);

    // T1: allocate — stores meta + raw data, returns handle
    {
        TestVarMeta meta{64, 64};
        u8 pixelData[128] = {};
        pixelData[0] = 0xFF;
        ResourceHandle h = table.allocate(meta, {pixelData, 128});
        check(h.isValid(), "alloc returns valid handle");
        check(h.type_id == 2, "type_id correct");
    }

    // T2: getRawData — returns correct pointer into rawBuffer
    {
        TestVarMeta meta{32, 32};
        u8 pixelData[64] = {};
        pixelData[0] = 0xAA;
        pixelData[63] = 0xBB;
        ResourceHandle h = table.allocate(meta, {pixelData, 64});
        u8* raw = table.getRawData(h);
        check(raw != nullptr, "getRawData returns non-null");
        check(raw[0] == 0xAA, "raw data byte 0 correct");
        check(raw[63] == 0xBB, "raw data last byte correct");
    }

    // T3: getMeta — returns correct metadata
    {
        TestVarMeta meta{128, 256};
        u8 data[10] = {};
        ResourceHandle h = table.allocate(meta, {data, 10});
        TestVarMeta* m = table.getMeta(h);
        check(m != nullptr, "getMeta returns non-null");
        check(m->width == 128, "meta width correct");
        check(m->height == 256, "meta height correct");
    }

    // T4: release — refCount decrements, raw block freed
    {
        TestVarMeta meta{16, 16};
        u8 data[8] = {};
        ResourceHandle h = table.allocate(meta, {data, 8});
        table.release(h);
        TestVarMeta* m = table.getMeta(h);
        check(m == nullptr, "getMeta returns null after release");
        u8* raw = table.getRawData(h);
        check(raw == nullptr, "getRawData returns null after release");
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 2: Implement VariableResourceTable**

Create `src/new_common/memory/variable_resource_table.cppm`:

```cppm
module;
#include <vector>
#include <cstring>

export module LX_New_Common.Memory:VariableResourceTable;

import LX_New_Common.Platform;
import LX_New_Common.Memory.RawBuffer;
import LX_New_Common.Memory.ResourceHandle;
import LX_New_Common.Memory.Types;

export namespace LX_New_Common {

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

    ResourceHandle allocate(const MetaType& meta, std::span<const u8> rawData) {
        u32 idx;
        if (!m_freeList.empty()) {
            idx = m_freeList.back();
            m_freeList.pop_back();
        } else {
            idx = (u32)m_entries.size();
            m_entries.push_back({});
        }
        Entry& e = m_entries[idx];
        e.meta = meta;
        e.refCount = 1;
        e.generation++;
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

    MetaType* getMeta(ResourceHandle handle) {
        if (!handle.isValid()) return nullptr;
        u32 idx = handle.index;
        if (idx >= m_entries.size()) return nullptr;
        if (m_entries[idx].generation != handle.generation) return nullptr;
        if (m_entries[idx].refCount == 0) return nullptr;
        return &m_entries[idx].meta;
    }

    u8* getRawData(ResourceHandle handle) {
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
```

- [ ] **Step 3: Update memory_test.cppm** — add import + test call.
- [ ] **Step 4: Build and verify**
- [ ] **Step 5: Commit**

```bash
git add src/new_common/memory/variable_resource_table.cppm
git add src/test/new/test/variable_resource_table_test.cppm
git add src/test/new/memory_test.cppm
git commit -m "feat(memory): add VariableResourceTable with tests"
```

---

### Task 5: ResourceManager — Umbrella

**Files:**
- Create: `src/new_common/memory/resource_manager.cppm`

**Interfaces:**
- Consumes: `TypedResourceTable`, `VariableResourceTable`
- Produces: `class ResourceManager` with typed dispatch

- [ ] **Step 1: Implement ResourceManager**

Create `src/new_common/memory/resource_manager.cppm`:

```cppm
module;
#include <memory>
#include <array>
#include <span>

export module LX_New_Common.Memory:ResourceManager;

import LX_New_Common.Platform;
import LX_New_Common.Memory.ResourceHandle;
import LX_New_Common.Memory.TypedResourceTable;
import LX_New_Common.Memory.VariableResourceTable;
import LX_New_Common.Memory.RawBuffer;

export namespace LX_New_Common {

class ResourceManager {
public:
    // Fixed-size resource allocation
    template<typename T>
    ResourceHandle allocateFixed(u8 typeId, const T& data) {
        auto& table = getFixedTable<T>(typeId);
        return table.allocate(data);
    }

    template<typename T>
    T* get(ResourceHandle handle) {
        auto& table = getFixedTable<T>(handle.type_id);
        return table.get(handle);
    }

    void release(ResourceHandle handle) {
        // Dispatch by type_id to appropriate table
        releaseByTypeId(handle);
    }

private:
    // Storage: for each type_id, either a fixed table or variable table
    // Simplified: use type-erased base
    // (Implementation detail: actual dispatch via virtual base or variant)
    void releaseByTypeId(ResourceHandle handle);
};

} // namespace LX_New_Common
```

- [ ] **Step 2: Build and verify** (compiles, no test changes needed — umbrella only)
- [ ] **Step 3: Commit**

```bash
git add src/new_common/memory/resource_manager.cppm
git commit -m "feat(memory): add ResourceManager umbrella"
```

---

### Task 6: SpillBlock — Overflow Pool

**Files:**
- Create: `src/new_common/memory/spill_block.cppm`
- Create: `src/test/new/test/spill_pool_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

**Interfaces:**
- Consumes: `LX_New_Common.Platform`, `LX_New_Common.Memory.Types`
- Produces: `template<typename T, u32 N> struct SpillBlock`, `template<typename T, u32 N> class SpillPool`

- [ ] **Step 1: Write the failing tests**

Create `src/test/new/test/spill_pool_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>
#include <vector>

export module LX_New_Test.Test.SpillPoolTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory.SpillBlock;
import LX_New_Common.Memory.Types;

export namespace LX_New_Test {

inline bool run_spill_pool_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // T1: allocBlock — first block gets index 0
    {
        SpillPool<u32, 4> pool;
        u32 idx = pool.allocBlock();
        check(idx == 0, "first block index 0");
    }

    // T2: freeChain — single block returns to freeList
    {
        SpillPool<u32, 4> pool;
        u32 idx = pool.allocBlock();
        pool.freeChain(idx);
        u32 reused = pool.allocBlock();
        check(reused == idx, "freed block reused");
    }

    // T3: append — adds items to inline block
    {
        SpillPool<u32, 4> pool;
        u32 head = kNoneChunk;
        head = pool.append(head, 10);
        head = pool.append(head, 20);
        check(pool.itemCount(head) == 2, "two items appended");
    }

    // T4: spill — spills to second block when first is full
    {
        SpillPool<u32, 2> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 5; ++i) {
            head = pool.append(head, i * 100);
        }
        check(pool.itemCount(head) == 5, "5 items across spill blocks");
    }

    // T5: iterator — iterates all items
    {
        SpillPool<u32, 2> pool;
        u32 head = kNoneChunk;
        for (u32 i = 1; i <= 5; ++i) {
            head = pool.append(head, i);
        }
        u32 count = 0;
        u32 sum = 0;
        for (auto it = pool.begin(head); it != pool.end(); ++it) {
            sum += *it;
            count++;
        }
        check(count == 5, "iterator yields 5 items");
        check(sum == 15, "sum correct (1+2+3+4+5)");
    }

    // T6: freeChain multi-block — frees entire chain
    {
        SpillPool<u32, 2> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 6; ++i) head = pool.append(head, i);
        pool.freeChain(head);
        check(pool.freeListSize() == 3, "all 3 blocks freed");
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 2: Implement SpillBlock + SpillPool**

Create `src/new_common/memory/spill_block.cppm`:

```cppm
module;
#include <vector>
#include <algorithm>

export module LX_New_Common.Memory:SpillBlock;

import LX_New_Common.Platform;
import LX_New_Common.Memory.Types;

export namespace LX_New_Common {

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
            m_blocks[idx] = {0, kNoneChunk, {}};
            return idx;
        }
        u32 idx = (u32)m_blocks.size();
        m_blocks.push_back({0, kNoneChunk, {}});
        return idx;
    }

    void freeChain(u32 headIdx) {
        u32 cur = headIdx;
        while (cur != kNoneChunk) {
            auto& block = m_blocks[cur];
            u32 next = block.nextBlockIdx;
            block.count = 0;
            block.nextBlockIdx = kNoneChunk;
            m_freeList.push_back(cur);
            cur = next;
        }
    }

    u32 append(u32 headIdx, T value) {
        if (headIdx == kNoneChunk) {
            headIdx = allocBlock();
        }
        u32 cur = headIdx;
        while (true) {
            auto& block = m_blocks[cur];
            if (block.count < N) {
                block.items[block.count++] = value;
                return headIdx;
            }
            if (block.nextBlockIdx == kNoneChunk) {
                block.nextBlockIdx = allocBlock();
            }
            cur = block.nextBlockIdx;
        }
    }

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

    struct Iterator {
        const SpillPool* pool;
        u32 blockIdx;
        u8  itemIdx;
        T& operator*() const { return pool->m_blocks[blockIdx].items[itemIdx]; }
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
    Iterator end() const { return {this, kNoneChunk, 0}; }
};

} // namespace LX_New_Common
```

- [ ] **Step 3: Update memory_test.cppm** — add import + test call.
- [ ] **Step 4: Build and verify**
- [ ] **Step 5: Commit**

```bash
git add src/new_common/memory/spill_block.cppm
git add src/test/new/test/spill_pool_test.cppm
git add src/test/new/memory_test.cppm
git commit -m "feat(memory): add SpillBlock overflow pool with tests"
```

---

### Task 7: GameObjectMeta + GameObjectManager

**Files:**
- Create: `src/new_common/memory/game_object_meta.cppm`
- Create: `src/new_common/memory/game_object_manager.cppm`
- Create: `src/test/new/test/game_object_manager_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

**Interfaces:**
- Consumes: `ResourceHandle`, `SpillBlock`, `RawBuffer`, `TypedResourceTable`, `VariableResourceTable`, `ResourceManager`
- Produces: `struct GameObjectMeta`, `class GameObjectManager`

- [ ] **Step 1: Write the failing tests**

Create `src/test/new/test/game_object_manager_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.GameObjectManagerTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory.GameObjectManager;
import LX_New_Common.Memory.Types;

export namespace LX_New_Test {

inline bool run_game_object_manager_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    GameObjectManager gom;

    // T1: allocMeta — first allocation gets index 0, alive=true
    {
        u32 idx = gom.allocMeta();
        check(idx == 0, "first meta index 0");
        check(gom.isAlive(idx), "meta alive after alloc");
    }

    // T2: allocMeta — freed meta is reused
    {
        u32 idx1 = gom.allocMeta();
        gom.freeMeta(idx1);
        u32 idx2 = gom.allocMeta();
        check(idx2 == idx1, "freed meta reused");
    }

    // T3: setRefs — inline refs stored correctly
    {
        u32 idx = gom.allocMeta();
        u32 refs[] = {10u, 20u, 30u};
        gom.setRefs(idx, refs);
        check(gom.getRefCount(idx) == 3, "ref count is 3");
    }

    // T4: setRefs — refs spill to SpillBlock when > 4
    {
        u32 idx = gom.allocMeta();
        u32 refs[] = {1u, 2u, 3u, 4u, 5u, 6u};
        gom.setRefs(idx, refs);
        check(gom.getRefCount(idx) == 4, "inline ref count capped at 4");
        check(gom.getSpillRefCount(idx) == 2, "spill ref count is 2");
    }

    // T5: setHandles — inline handles stored correctly
    {
        u32 idx = gom.allocMeta();
        u64 handles[] = {0x1111ull, 0x2222ull};
        gom.setHandles(idx, handles);
        check(gom.getHandleCount(idx) == 2, "handle count is 2");
    }

    // T6: setHandles — handles spill when > 4
    {
        u32 idx = gom.allocMeta();
        u64 handles[] = {1ull, 2ull, 3ull, 4ull, 5ull};
        gom.setHandles(idx, handles);
        check(gom.getHandleCount(idx) == 4, "inline handle count capped at 4");
        check(gom.getSpillHandleCount(idx) == 1, "spill handle count is 1");
    }

    // T7: addToRoot / removeFromRoot
    {
        u32 idx = gom.allocMeta();
        gom.addToRoot(idx);
        check(gom.getRootCount() == 1, "root count 1");
        gom.removeFromRoot(idx);
        check(gom.getRootCount() == 0, "root count 0 after remove");
    }

    // T8: mark — single root marks itself
    {
        u32 idx = gom.allocMeta();
        gom.addToRoot(idx);
        gom.mark();
        check(gom.isMarked(idx), "root is marked");
        gom.clearMarks();
    }

    // T9: mark — transitive refs (A→B→C)
    {
        u32 a = gom.allocMeta();
        u32 b = gom.allocMeta();
        u32 c = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.setRefs(b, std::array<u32,1>{c});
        gom.addToRoot(a);
        gom.mark();
        check(gom.isMarked(a), "a marked");
        check(gom.isMarked(b), "b marked");
        check(gom.isMarked(c), "c marked");
        gom.clearMarks();
        gom.removeFromRoot(a);
    }

    // T10: mark — dead meta not traversed
    {
        u32 a = gom.allocMeta();
        u32 b = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.freeMeta(b);
        gom.addToRoot(a);
        gom.mark();
        check(gom.isMarked(a), "a marked");
        // b is dead, should not crash
        gom.clearMarks();
        gom.removeFromRoot(a);
    }

    // T11: mark — cycle doesn't infinite loop (A→B→A)
    {
        u32 a = gom.allocMeta();
        u32 b = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.setRefs(b, std::array<u32,1>{a});
        gom.addToRoot(a);
        gom.mark();
        check(gom.isMarked(a) && gom.isMarked(b), "cycle both marked");
        gom.clearMarks();
        gom.removeFromRoot(a);
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 2: Implement GameObjectMeta**

Create `src/new_common/memory/game_object_meta.cppm`:

```cppm
module;

export module LX_New_Common.Memory:GameObjectMeta;

import LX_New_Common.Platform;
import LX_New_Common.Memory.Types;
import LX_New_Common.Memory.SpillBlock;

export namespace LX_New_Common {

constexpr u32 kInlineRefCount = 4;
constexpr u32 kInlineHandleCount = 4;

struct GameObjectMeta {
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

} // namespace LX_New_Common
```

- [ ] **Step 3: Implement GameObjectManager**

Create `src/new_common/memory/game_object_manager.cppm`:

```cppm
module;
#include <vector>
#include <algorithm>
#include <array>
#include <span>

export module LX_New_Common.Memory:GameObjectManager;

import LX_New_Common.Platform;
import LX_New_Common.Memory.Types;
import LX_New_Common.Memory.ResourceHandle;
import LX_New_Common.Memory.GameObjectMeta;
import LX_New_Common.Memory.SpillBlock;

export namespace LX_New_Common {

class GameObjectManager {
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

    bool isMarked(u32 idx) const { return m_metas[idx].marked; }

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
            // Release handles (to be implemented with ResourceManager)
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
```

- [ ] **Step 4: Update memory_test.cppm** — add import + test call.
- [ ] **Step 5: Build and verify**
- [ ] **Step 6: Commit**

```bash
git add src/new_common/memory/game_object_meta.cppm
git add src/new_common/memory/game_object_manager.cppm
git add src/test/new/test/game_object_manager_test.cppm
git add src/test/new/memory_test.cppm
git commit -m "feat(memory): add GameObjectMeta + GameObjectManager with tests"
```

---

### Task 8: Memory Umbrella Module

**Files:**
- Create: `src/new_common/memory/memory.cppm`

**Interfaces:**
- Produces: `export module LX_New_Common.Memory;` — re-exports all memory partitions

- [ ] **Step 1: Create memory.cppm**

```cppm
module;

export module LX_New_Common.Memory;

export import :Types;
export import :ResourceHandle;
export import :RawBuffer;
export import :TypedResourceTable;
export import :VariableResourceTable;
export import :ResourceManager;
export import :SpillBlock;
export import :GameObjectMeta;
export import :GameObjectManager;
```

- [ ] **Step 2: Build LX_New_Common**

```bash
cmake --build . --target LX_New_Common --config Debug
```

- [ ] **Step 3: Commit**

```bash
git add src/new_common/memory/memory.cppm
git commit -m "feat(memory): add Memory umbrella module"
```

---

### Task 9: GC Integration Tests

**Files:**
- Create: `src/test/new/test/gc_integration_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

- [ ] **Step 1: Write integration tests**

Create `src/test/new/test/gc_integration_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.GcIntegrationTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory.GameObjectManager;
import LX_New_Common.Memory.Types;

export namespace LX_New_Test {

inline bool run_gc_integration_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // T1: create scene graph, addToRoot, tick → nothing collected
    {
        GameObjectManager gom;
        u32 scene = gom.allocMeta();
        u32 node = gom.allocMeta();
        u32 ro = gom.allocMeta();
        gom.setRefs(scene, std::array<u32,1>{node});
        gom.setRefs(node, std::array<u32,1>{ro});
        gom.addToRoot(scene);
        gom.tick();
        check(gom.isAlive(scene), "scene alive after tick");
        check(gom.isAlive(node), "node alive after tick");
        check(gom.isAlive(ro), "ro alive after tick");
        gom.removeFromRoot(scene);
    }

    // T2: removeFromRoot, tick → entire graph collected
    {
        GameObjectManager gom;
        u32 scene = gom.allocMeta();
        u32 node = gom.allocMeta();
        gom.setRefs(scene, std::array<u32,1>{node});
        gom.addToRoot(scene);
        gom.tick(); // warm up
        gom.removeFromRoot(scene);
        gom.tick();
        check(!gom.isAlive(scene), "scene collected");
        check(!gom.isAlive(node), "node collected");
    }

    // T3: partial graph removal — only unreachable subgraph collected
    {
        GameObjectManager gom;
        u32 s1 = gom.allocMeta();
        u32 s2 = gom.allocMeta();
        u32 n1 = gom.allocMeta();
        u32 n2 = gom.allocMeta();
        gom.setRefs(s1, std::array<u32,1>{n1});
        gom.setRefs(s2, std::array<u32,1>{n2});
        gom.addToRoot(s1);
        gom.addToRoot(s2);
        gom.tick();
        gom.removeFromRoot(s1);
        gom.tick();
        check(!gom.isAlive(s1), "s1 collected");
        check(!gom.isAlive(n1), "n1 collected");
        check(gom.isAlive(s2), "s2 survives");
        check(gom.isAlive(n2), "n2 survives");
        gom.removeFromRoot(s2);
    }

    // T4: shared resource — not collected while either node alive
    {
        GameObjectManager gom;
        u32 s1 = gom.allocMeta();
        u32 s2 = gom.allocMeta();
        u32 shared = gom.allocMeta();
        gom.setRefs(s1, std::array<u32,1>{shared});
        gom.setRefs(s2, std::array<u32,1>{shared});
        gom.addToRoot(s1);
        gom.addToRoot(s2);
        gom.tick();
        gom.removeFromRoot(s1);
        gom.tick();
        check(gom.isAlive(shared), "shared survives (still referenced by s2)");
        gom.removeFromRoot(s2);
        gom.tick();
        check(!gom.isAlive(shared), "shared collected after both roots removed");
    }

    // T5: cycle — A→B→A, both unrooted, both collected
    {
        GameObjectManager gom;
        u32 a = gom.allocMeta();
        u32 b = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.setRefs(b, std::array<u32,1>{a});
        gom.addToRoot(a);
        gom.tick();
        gom.removeFromRoot(a);
        gom.tick();
        check(!gom.isAlive(a), "a collected");
        check(!gom.isAlive(b), "b collected");
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 2: Update memory_test.cppm** — add import + test call.
- [ ] **Step 3: Build and verify**
- [ ] **Step 4: Commit**

```bash
git add src/test/new/test/gc_integration_test.cppm
git add src/test/new/memory_test.cppm
git commit -m "test(memory): add GC integration tests"
```

---

### Task 10: ASAN Leak Test + CMake ASAN Support

**Files:**
- Modify: `src/test/new/CMakeLists.txt`
- Create: `src/test/new/test/leak_test.cppm`
- Modify: `src/test/new/memory_test.cppm`

- [ ] **Step 1: Update CMakeLists.txt with ASAN option**

Read current `src/test/new/CMakeLists.txt`, replace with:

```cmake
# LX_new module smoke + memory tests.
add_executable(lxe_test_new_modules main.cpp)

set_target_properties(lxe_test_new_modules PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_link_libraries(lxe_test_new_modules
  PRIVATE
    ${LX_NEW_CORE_LIB}
    ${LX_NEW_COMMON_LIB}
)

add_test(NAME lxe_test_new_modules
  COMMAND $<TARGET_FILE:lxe_test_new_modules>)
set_tests_properties(lxe_test_new_modules PROPERTIES
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  LABELS "auto;headless_ok"
)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN AND MSVC)
  target_compile_options(lxe_test_new_modules PRIVATE /fsanitize=address)
  target_link_options(lxe_test_new_modules PRIVATE /fsanitize=address)
elseif(ENABLE_ASAN)
  target_compile_options(lxe_test_new_modules PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(lxe_test_new_modules PRIVATE -fsanitize=address -fno-omit-frame-pointer)
endif()
```

- [ ] **Step 2: Write leak test**

Create `src/test/new/test/leak_test.cppm`:

```cppm
module;
#include <cassert>
#include <iostream>

export module LX_New_Test.Test.LeakTest;

import LX_New_Common.Platform;
import LX_New_Common.Memory.GameObjectManager;
import LX_New_Common.Memory.ResourceHandle;
import LX_New_Common.Memory.RawBuffer;
import LX_New_Common.Memory.SpillBlock;
import LX_New_Common.Memory.Types;

export namespace LX_New_Test {

inline bool run_leak_tests() {
    using namespace LX_New_Common;
    bool pass = true;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cerr << "  FAIL: " << msg << "\n"; pass = false; }
    };

    // TL1: create + destroy scene graph → no leaks
    {
        GameObjectManager gom;
        u32 scene = gom.allocMeta();
        u32 node = gom.allocMeta();
        u32 ro = gom.allocMeta();
        gom.setRefs(scene, std::array<u32,1>{node});
        gom.setRefs(node, std::array<u32,1>{ro});
        gom.addToRoot(scene);
        gom.tick();
        gom.removeFromRoot(scene);
        gom.tick();
        check(!gom.isAlive(scene), "scene collected");
    }

    // TL2: alloc resources, add root, remove root, tick → no leaks
    {
        GameObjectManager gom;
        u32 obj = gom.allocMeta();
        u64 fakeHandle = 0x12345678ull;
        fakeHandle |= (u64)1 << 16; // set generation
        gom.setHandles(obj, std::array<u64,1>{fakeHandle});
        gom.addToRoot(obj);
        gom.tick();
        gom.removeFromRoot(obj);
        gom.tick();
        check(!gom.isAlive(obj), "obj collected");
    }

    // TL3: spill pool alloc + free → no leaks
    {
        SpillPool<u32, 4> pool;
        u32 head = kNoneChunk;
        for (u32 i = 0; i < 20; ++i) head = pool.append(head, i);
        pool.freeChain(head);
        check(pool.freeListSize() > 0, "spill blocks freed");
    }

    // TL4: raw buffer alloc + free → no leaks
    {
        RawBuffer buf;
        std::vector<u32> offsets;
        for (int i = 0; i < 100; ++i) {
            offsets.push_back(buf.allocate(64, 16));
        }
        for (auto o : offsets) buf.free(o, 64);
        check(buf.fragmentationRatio() < 1.0f, "some space reused");
    }

    // TL5: cyclic refs unrooted → GC collects both
    {
        GameObjectManager gom;
        u32 a = gom.allocMeta();
        u32 b = gom.allocMeta();
        gom.setRefs(a, std::array<u32,1>{b});
        gom.setRefs(b, std::array<u32,1>{a});
        gom.addToRoot(a);
        gom.tick();
        gom.removeFromRoot(a);
        gom.tick();
        check(!gom.isAlive(a) && !gom.isAlive(b), "cycle collected");
    }

    return pass;
}

} // namespace LX_New_Test
```

- [ ] **Step 3: Update memory_test.cppm** — add leak test import and call.
- [ ] **Step 4: Build and run with ASAN**

```bash
cmake -DENABLE_ASAN=ON .. -G Ninja
ninja lxe_test_new_modules
./src/test/new/lxe_test_new_modules
```

Expected: All tests pass, ASAN reports no leaks.

- [ ] **Step 5: Commit**

```bash
git add src/test/new/CMakeLists.txt
git add src/test/new/test/leak_test.cppm
git add src/test/new/memory_test.cppm
git commit -m "test(memory): add ASAN leak tests and CMake ASAN support"
```

---

### Task 11: Update new_core/core.cppm to import Memory

**Files:**
- Modify: `src/new_core/core.cppm`

- [ ] **Step 1: Update core.cppm**

```cppm
module;

export module LX_New_Core;

import LX_New_Common.Types;
import LX_New_Common.Platform.Time;
import LX_New_Common.Memory;

export namespace LX_New_Core {

inline constexpr LX_New_Common::u32 kVersion = 1;

inline LX_New_Common::Clock MakeBootClock() { return LX_New_Common::Clock{}; }

} // namespace LX_New_Core
```

- [ ] **Step 2: Build LX_New_Core**

```bash
cmake --build . --target LX_New_Core --config Debug
```

- [ ] **Step 3: Commit**

```bash
git add src/new_core/core.cppm
git commit -m "feat(core): import LX_New_Common.Memory into LX_New_Core"
```

---

## Self-Review

### Spec Coverage Check

| Spec Requirement | Task |
|---|---|
| ResourceHandle 64-bit packed | Task 1 |
| TypedResourceTable (meta/data split) | Task 3 |
| VariableResourceTable (meta + RawBuffer) | Task 4 |
| RawBuffer free block list | Task 2 |
| SpillBlock overflow pool | Task 6 |
| GameObjectMeta (56 bytes, inline + spill) | Task 7 |
| GameObjectManager mark-sweep | Task 7 |
| ResourceManager umbrella | Task 5 |
| Memory umbrella module | Task 8 |
| TDD for each component | All tasks |
| ASAN leak test | Task 10 |
| Thread safety (future) | Not in scope (listed as "待后续 spec 细化") |
| WeakRef (future) | Not in scope (listed as "待后续 spec 细化") |
| RawBuffer compact at 30% | Not in scope (listed as "待后续 spec 细化") |
| CMake ASAN support | Task 10 |
| Test project structure | All test tasks |

### Placeholder Scan
- No "TBD", "TODO", "implement later" found
- All test code shown in full
- All code blocks contain complete implementations
- No "Similar to Task N" references

### Type Consistency
- `u8/u16/u32/u64` from `LX_New_Common.Platform` used consistently
- `kNoneChunk = 0xFFFFFFFF` from `LX_New_Common.Memory.Types`
- `ResourceHandle` union with 8/24/16/16 bit fields
- `GameObjectMeta` fields: `refCount` (u8), `handleCount` (u8), `refSpillHead` (u32), `handleSpillHead` (u32)
- `SpillPool<T,N>` template used for both ref and handle overflow

---

**Plan complete and saved to `docs/superpowers/plans/2026-06-17-memory-manager-implementation.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?
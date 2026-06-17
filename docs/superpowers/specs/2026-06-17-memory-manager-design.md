# Memory Manager 设计

## 目标

为 LXEngine 提供显式的、统一管理的内存系统。核心约束：
- Resource 数据连续排布，可直接批量上传 GPU
- Resource 按引用计数管理，返回 Handle
- GameObject 通过 mark-sweep GC 管理，从 Root 出发遍历
- GameObject 析构时递归释放持有的 Resource Handle
- 每帧 tick 驱动 GC

---

## 架构分层

```
ResourceManager（引用计数 + Handle）
    ↑
GameObjectManager（mark-sweep GC + meta 管理）
    ↑
Scene / Asset 创建（通过 manager 分配，不直接 new）
```

---

## ResourceHandle（64-bit packed）

```cpp
union ResourceHandle {
    u64 raw;
    struct {
        u64 type_id    : 8;   // 256 种资源类型
        u64 index      : 24;  // 每类型最多 16,777,216 个条目
        u64 generation : 16;  // 65536 世代，防悬垂引用
        u64 _reserved  : 16;  // 预留
    };
    
    bool isValid() const { return generation != 0; }
    static ResourceHandle invalid() { return ResourceHandle{0}; }
};
```

**位分配理由：**
- type_id 8 bit → 256 种类型，远超当前需求（VertexBuffer/IndexBuffer/TextureBuffer/ParameterBuffer ≈ 4 种）
- index 24 bit → 16M，中大型场景都够用
- generation 16 bit → 65536，同一个槽位被回收 65536 次后才复用，实际不可能发生
- reserved 16 bit → 未来扩展（如 debug tag、alignment hint 等）

### 单元测试

```cpp
// test/resource_handle_test.cppm
// T1: isValid — invalid handle returns false
// T2: isValid — valid handle with generation != 0 returns true
// T3: packing/unpacking — type_id=3, index=1000, generation=5 round-trips correctly
// T4: reserved bits survive round-trip
```

---

## ResourceManager — Table 结构

### 第一层：按类型索引

```cpp
constexpr u32 MaxResourceTypes = 256;

class ResourceManager {
    ResourceTypeTableBase* m_tables[MaxResourceTypes];
};
```

`ResourceTypeTableBase` 是虚基类，每类资源有具体的 `TypedResourceTable<T>` 或 `VariableResourceTable<Meta>`。

### 第二层：固定大小资源 → meta 与 data 分离

```cpp
template<typename T>
struct TypedResourceTable {
    struct Meta {
        u16 refCount;
        u16 generation;
    };

    std::vector<Meta>  metas;           // 元数据数组（CPU 端管理引用/世代）
    std::vector<T>     data;            // 业务数据数组（连续，可直接上传 GPU）
    std::vector<u32>   freeList;        // 空闲槽位索引（LIFO stack）

    ResourceHandle allocate(const T& dataItem) {
        u32 idx;
        if (!freeList.empty()) {
            idx = freeList.back();
            freeList.pop_back();
            metas[idx] = {1, metas[idx].generation + 1};
            data[idx] = dataItem;
        } else {
            idx = (u32)metas.size();
            metas.push_back({1, 1});
            data.push_back(dataItem);
        }
        return ResourceHandle{typeId, idx, metas[idx].generation};
    }

    void release(ResourceHandle handle) {
        u32 idx = handle.index;
        if (metas[idx].generation != handle.generation) return; // 过期 handle
        metas[idx].refCount--;
        if (metas[idx].refCount == 0) {
            freeList.push_back(idx);
        }
    }

    T* get(ResourceHandle handle) {
        if (!handle.isValid()) return nullptr;
        if (metas[handle.index].generation != handle.generation) return nullptr;
        return &data[handle.index];
    }
};
```

### 第二层：可变长资源 → meta + raw buffer

```cpp
struct VariableResourceMeta {
    u16 refCount;
    u16 generation;
    u32 rawOffset;     // 在 raw buffer 中的偏移
    u32 rawSize;       // 占用大小
    // 后面跟该类型特有的元数据字段
};

template<typename MetaType>
struct VariableResourceTable {
    std::vector<MetaType>    entries;
    std::vector<u32>         freeList;
    RawBuffer                rawBuffer;

    ResourceHandle allocate(const MetaType& meta, std::span<const u8> rawData);
    void           release(ResourceHandle handle);
    MetaType*      getMeta(ResourceHandle handle);
    u8*            getRawData(ResourceHandle handle);
};
```

### RawBuffer — Free Block List

```cpp
class RawBuffer {
    std::vector<u8> m_data;          // 大 buffer，初始预分配（如 64MB）
    
    struct FreeBlock {
        u32 offset;
        u32 size;
    };
    std::vector<FreeBlock> m_freeBlocks;  // 按 offset 排序，支持合并相邻空闲块

    u32 allocate(u32 size, u32 alignment);  // 返回 offset
    void  free(u32 offset, u32 size);       // 加入 free list，合并相邻
};
```

**分配流程：** first-fit，可拆分为分配块 + 剩余块放回 free list。无可用块时 `m_data` 扩容。

**释放流程：** 插入 free block（保持 offset 排序），合并相邻空闲块。碎片率 > 30% 时 compact（后续优化）。

### 单元测试

```cpp
// test/typed_resource_table_test.cppm
// T1: allocate — first allocation gets index 0, generation 1
// T2: allocate — second allocation gets index 1
// T3: get — returns valid pointer to data
// T4: release — refCount decrements, block not freed while refCount > 0
// T5: release — refCount reaches 0, index goes to freeList
// T6: reuse — freed slot is reused with generation + 1
// T7: stale handle — get() returns nullptr for expired generation

// test/variable_resource_table_test.cppm
// T1: allocate — stores meta + raw data, returns handle
// T2: getRawData — returns correct pointer into rawBuffer
// T3: release — refCount decrements, raw block freed
// T4: shared raw — two handles to same raw data (via shared meta)

// test/raw_buffer_test.cppm
// T1: allocate — first allocation at offset 0
// T2: allocate — second allocation contiguous after first
// T3: free — frees a block, adds to freeList
// T4: merge — freeing adjacent blocks merges them into one
// T5: reuse — allocates from freed block (first-fit)
// T6: alignment — respects requested alignment
```

---

## SpillBlock — 溢出池

当 inline 容量（4 个 ref / 4 个 handle）不足时，溢出池用链表块存储。

```cpp
constexpr u32 kNoneChunk = 0xFFFFFFFF;

template<typename T, u32 N = 8>
struct SpillBlock {
    u8  count;          // 本块有效项数
    u32 nextBlockIdx;   // kNoneChunk = 链尾
    T   items[N];
};
```

### GameObjectManager 成员

```cpp
class GameObjectManager {
    std::vector<SpillBlock<u32>>  m_refBlocks;
    std::vector<u32>              m_refFreeList;
    std::vector<SpillBlock<u64>>  m_handleBlocks;
    std::vector<u32>              m_handleFreeList;

    u32 allocRefBlock() {
        if (!m_refFreeList.empty()) {
            u32 idx = m_refFreeList.back();
            m_refFreeList.pop_back();
            m_refBlocks[idx] = {0, kNoneChunk, {}};
            return idx;
        }
        u32 idx = (u32)m_refBlocks.size();
        m_refBlocks.push_back({0, kNoneChunk, {}});
        return idx;
    }

    void freeRefChain(u32 headIdx) {
        u32 cur = headIdx;
        while (cur != kNoneChunk) {
            auto& block = m_refBlocks[cur];
            u32 next = block.nextBlockIdx;
            block.count = 0;
            block.nextBlockIdx = kNoneChunk;
            m_refFreeList.push_back(cur);
            cur = next;
        }
    }

    u32 appendRef(u32 headIdx, u32 targetIdx) {
        if (headIdx == kNoneChunk) {
            headIdx = allocRefBlock();
        }
        u32 cur = headIdx;
        while (true) {
            auto& block = m_refBlocks[cur];
            if (block.count < 8) {
                block.items[block.count++] = targetIdx;
                return headIdx;
            }
            if (block.nextBlockIdx == kNoneChunk) {
                block.nextBlockIdx = allocRefBlock();
            }
            cur = block.nextBlockIdx;
        }
    }

    // 遍历器
    struct RefIterator {
        const std::vector<SpillBlock<u32>>* blocks;
        u32 blockIdx;
        u8  itemIdx;
        u32 operator*() const { return (*blocks)[blockIdx].items[itemIdx]; }
        void operator++() {
            auto& block = (*blocks)[blockIdx];
            itemIdx++;
            if (itemIdx >= block.count) {
                blockIdx = block.nextBlockIdx;
                itemIdx = 0;
            }
        }
        bool operator!=(const RefIterator& o) const { return blockIdx != o.blockIdx; }
    };
    RefIterator refBegin(u32 headIdx) const { return {&m_refBlocks, headIdx, 0}; }
    RefIterator refEnd() const { return {&m_refBlocks, kNoneChunk, 0}; }

    // handle 的 appendRef/freeChain/iterator 同理，用 u64
};
```

### 单元测试

```cpp
// test/spill_pool_test.cppm
// T1: allocBlock — first block gets index 0
// T2: freeChain — single block chain returns to freeList
// T3: freeChain — multi-block chain frees all blocks
// T4: appendRef — fills inline, then spill block
// T5: appendRef — spills to second block when first is full (8 items)
// T6: iterator — iterates all items across multiple blocks
// T7: reuse — freed blocks are reused by allocBlock
```

---

## GameObjectManager — Mark-Sweep GC

### GameObjectMeta（固定大小 + spill head）

```cpp
// 每个 GameObject 对应一条 meta 记录（56 bytes）
struct GameObjectMeta {
    u8  marked : 1;         // mark-sweep: 0=未标记, 1=已标记
    u8  alive  : 1;         // 是否有效（sweep 后设为 0）
    u8  _pad   : 6;

    // ========== Inline refs ==========
    u8  refCount;           // inline 0-4
    u32 refs[4];
    u32 refSpillHead;       // m_refBlocks 中的链头索引，kNoneChunk = 无溢出

    // ========== Inline handles ==========
    u8  handleCount;        // inline 0-4
    u64 handles[4];
    u32 handleSpillHead;    // m_handleBlocks 中的链头索引
};
// sizeof = 4 + 1 + 4*4 + 4 + 1 + 8*4 + 4 = 52 bytes（对齐后 56 bytes）
```

### GameObjectManager 完整成员

```cpp
class GameObjectManager {
    std::vector<GameObjectMeta> m_metas;
    std::vector<u32>            m_freeMetaList;
    std::vector<u32>            m_roots;

    // Spill pools
    std::vector<SpillBlock<u32>>  m_refBlocks;
    std::vector<u32>              m_refFreeList;
    std::vector<SpillBlock<u64>>  m_handleBlocks;
    std::vector<u32>              m_handleFreeList;
};
```

### 添加/设置引用关系

```cpp
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

void setRefs(u32 metaIdx, std::span<const u32> targets) {
    auto& meta = m_metas[metaIdx];
    meta.refCount = 0;
    meta.refSpillHead = kNoneChunk;
    for (u32 t : targets) {
        if (meta.refCount < 4) {
            meta.refs[meta.refCount++] = t;
        } else {
            meta.refSpillHead = appendRef(meta.refSpillHead, t);
        }
    }
}

void setHandles(u32 metaIdx, std::span<const u64> handles) {
    auto& meta = m_metas[metaIdx];
    meta.handleCount = 0;
    meta.handleSpillHead = kNoneChunk;
    for (u64 h : handles) {
        if (meta.handleCount < 4) {
            meta.handles[meta.handleCount++] = h;
        } else {
            meta.handleSpillHead = appendHandle(meta.handleSpillHead, h);
        }
    }
}
```

### Root 管理

```cpp
void addToRoot(u32 metaIdx) {
    m_roots.push_back(metaIdx);
}

void removeFromRoot(u32 metaIdx) {
    auto it = std::find(m_roots.begin(), m_roots.end(), metaIdx);
    if (it != m_roots.end()) {
        *it = m_roots.back();
        m_roots.pop_back();
    }
}
```

### Mark-Sweep 算法

```cpp
void tick() {
    mark();
    sweep();
}

void mark() {
    for (auto& meta : m_metas) {
        if (meta.alive) meta.marked = false;
    }
    for (u32 rootIdx : m_roots) {
        markRecursive(rootIdx);
    }
}

void markRecursive(u32 metaIdx) {
    if (metaIdx == kInvalidMetaIdx) return;
    auto& meta = m_metas[metaIdx];
    if (!meta.alive || meta.marked) return;
    meta.marked = true;

    // inline refs
    for (u8 i = 0; i < meta.refCount; ++i) {
        markRecursive(meta.refs[i]);
    }
    // spill refs
    for (auto it = refBegin(meta.refSpillHead); it != refEnd(); ++it) {
        markRecursive(*it);
    }
}

void sweep() {
    for (u32 i = 0; i < m_metas.size(); ++i) {
        auto& meta = m_metas[i];
        if (!meta.alive || meta.marked) continue;

        // 1. 释放 inline handles
        for (u8 j = 0; j < meta.handleCount; ++j) {
            u64 raw = meta.handles[j];
            if (raw != kInvalidHandle) {
                m_resourceManager.release(ResourceHandle{raw});
            }
        }
        // 2. 释放 spill handles
        if (meta.handleSpillHead != kNoneChunk) {
            u32 cur = meta.handleSpillHead;
            while (cur != kNoneChunk) {
                auto& block = m_handleBlocks[cur];
                for (u8 j = 0; j < block.count; ++j) {
                    u64 raw = block.items[j];
                    if (raw != kInvalidHandle) {
                        m_resourceManager.release(ResourceHandle{raw});
                    }
                }
                u32 next = block.nextBlockIdx;
                block.count = 0;
                block.nextBlockIdx = kNoneChunk;
                m_handleFreeList.push_back(cur);
                cur = next;
            }
            meta.handleSpillHead = kNoneChunk;
        }
        // 3. 释放 spill ref 链
        if (meta.refSpillHead != kNoneChunk) {
            freeRefChain(meta.refSpillHead);
            meta.refSpillHead = kNoneChunk;
        }
        // 4. 重置 meta
        meta.alive = false;
        meta.marked = false;
        meta.refCount = 0;
        meta.handleCount = 0;
        // 5. 放回 free list
        m_freeMetaList.push_back(i);
    }
}
```

### 单元测试

```cpp
// test/game_object_manager_test.cppm
// T1: allocMeta — first allocation gets index 0, alive=true
// T2: allocMeta — freed meta is reused
// T3: setRefs — inline refs stored correctly (≤ 4)
// T4: setRefs — refs spill to SpillBlock when > 4
// T5: setHandles — inline handles stored correctly (≤ 4)
// T6: setHandles — handles spill to SpillBlock when > 4
// T7: addToRoot / removeFromRoot — root list correct
// T8: mark — single root marks itself
// T9: mark — root marks transitive refs (A→B→C)
// T10: mark — dead meta (alive=false) not traversed
// T11: mark — already marked meta skips recursion (no infinite loop on cycles)
// T12: sweep — unmarked meta released, handles released to ResourceManager
// T13: sweep — spill handles released and spill chain freed
// T14: sweep — marked meta survives
// T15: tick full cycle — mark + sweep, all unrooted objects collected
// T16: cycle — A→B→A, both unrooted, both collected after removeFromRoot

// test/gc_integration_test.cppm
// T1: create scene graph, addToRoot, tick → nothing collected
// T2: removeFromRoot, tick → entire graph collected
// T3: partial graph removal → only unreachable subgraph collected
// T4: shared resource (mesh referenced by two nodes) → resource not collected while either node alive
// T5: handle release cascade — GameObject destruction cascades into ResourceManager::release
```

---

## 完整生命周期示例

```
1. 解析 YAML: "damaged_helmet" 节点
2. ResourceManager::allocate(VertexBuffer, rawData) → Handle h_vb
3. ResourceManager::allocate(IndexBuffer, rawData)  → Handle h_ib
4. AssetManager::create<GeometryStorage>(h_vb, h_ib) → StrongRef<GeometryStorage>
   → gameObjectManager.allocMeta() → metaIdx_gs
   → setHandles(metaIdx_gs, {h_vb.raw, h_ib.raw})
5. AssetManager::create<Mesh>(storageRef) → StrongRef<Mesh>
   → allocMeta() → metaIdx_mesh
   → setRefs(metaIdx_mesh, {metaIdx_gs})
6. AssetManager::create<MaterialInstance>(...) → StrongRef<MaterialInstance>
   → allocMeta() → metaIdx_material
7. AssetManager::create<RenderObject>(meshRef, materialRef) → StrongRef<RenderObject>
   → allocMeta() → metaIdx_ro
   → setRefs(metaIdx_ro, {metaIdx_mesh, metaIdx_material})
8. GameObjectManager::create<SceneNode>("damaged_helmet") → StrongRef<SceneNode>
   → allocMeta() → metaIdx_node
   → setRefs(metaIdx_node, {metaIdx_ro})
9. GameObjectManager::create<SceneInfo>("my_scene") → StrongRef<SceneInfo>
   → allocMeta() → metaIdx_scene
   → setRefs(metaIdx_scene, {metaIdx_node})
   → addToRoot(metaIdx_scene)

10. GC.tick():
    mark: roots[0]=metaIdx_scene
          → refs[metaIdx_scene] = {metaIdx_node} → mark node
          → refs[metaIdx_node] = {metaIdx_ro} → mark renderObject
          → refs[metaIdx_ro] = {metaIdx_mesh, metaIdx_material} → mark both
          → refs[metaIdx_mesh] = {metaIdx_gs} → mark geometryStorage
          沿途所有 meta.marked = true
    sweep: 无未标记 meta → 无回收

11. 卸载场景:
    removeFromRoot(metaIdx_scene)

12. GC.tick():
    mark: roots 为空 → 无标记
    sweep: 
      → metaIdx_scene 未标记 → 释放 handles（无）→ alive=false → freeMetaList
      → metaIdx_node 未标记 → 释放 handles（无）→ alive=false → freeMetaList
      → metaIdx_ro 未标记 → 释放 handles → m_resourceManager.release(h_ro) → alive=false
      → metaIdx_mesh 未标记 → 释放 handles → m_resourceManager.release(h_mesh) → alive=false
      → metaIdx_gs 未标记 → 释放 handles → m_resourceManager.release(h_vb), release(h_ib)
      → metaIdx_material 未标记 → 释放 handles → m_resourceManager.release(h_material)
      → ResourceManager: h_vb refCount=0 → 放回 freeList，释放 raw buffer
      → ResourceManager: h_ib refCount=0 → 同上
```

---

## 类关系总览

```
ResourceManager
├── TypedResourceTable<T>          固定大小资源（ParameterBuffer）
│   ├── Meta: {refCount, generation}
│   ├── data[]                     连续数组（可直接上传 GPU）
│   └── freeList[]                 空闲槽位
│
└── VariableResourceTable<Meta>    可变长资源（Vertex/Index/TextureBuffer）
    ├── entries[]                  meta 数组
    ├── freeList[]                 空闲 meta 槽位
    └── RawBuffer                  大 buffer + free block list

GameObjectManager
├── m_metas[]                      GameObjectMeta 数组（56 bytes/条）
├── m_freeMetaList[]               空闲 meta 槽位
├── m_roots[]                      root 索引
├── m_refBlocks[]                  溢出 ref 链表块
├── m_refFreeList[]                空闲 ref 块
├── m_handleBlocks[]               溢出 handle 链表块
└── m_handleFreeList[]             空闲 handle 块
    tick()                         mark + sweep

ResourceHandle (64-bit)
├── type_id(8) : 资源类型
├── index(24)  : 在 Table 中的索引
├── generation(16) : 世代号
└── _reserved(16)
```

---

## 测试项目结构

新建 `src/test/new/` 作为 new_test 项目，使用 C++20 modules 规范。

```
src/test/new/
  CMakeLists.txt
  test_main.cppm
  memory/
    resource_handle_test.cppm
    typed_resource_table_test.cppm
    variable_resource_table_test.cppm
    raw_buffer_test.cppm
    spill_pool_test.cppm
    game_object_manager_test.cppm
    gc_integration_test.cppm
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.28)
set(LX_NEW_TEST LX_New_Test)

add_executable(${LX_NEW_TEST})

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cppm")

target_sources(${LX_NEW_TEST}
  PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${TEST_SOURCES}
)

target_link_libraries(${LX_NEW_TEST} PRIVATE LX_New_Common LX_New_Core)
target_compile_features(${LX_NEW_TEST} PRIVATE cxx_std_20)
```

### 测试框架约定

- 每个 `.cppm` 文件声明 `export module LX_New_Test.Memory.XxxTest;`
- 测试入口 `test_main.cppm` 注册并运行所有测试
- 简单断言宏：`LX_TEST(name) { ... }` + `LX_ASSERT(condition, "message")`
- 不依赖第三方测试库，自行实现轻量测试 runner

---

## 已决策事项

| # | 问题 | 决策 |
|---|---|---|
| 1 | RawBuffer compact 时机 | **碎片率 > 30% 时自动触发 compact** |
| 2 | Partial GC | **暂不实现，每帧全量 GC 或手动 tick()** |
| 3 | 线程安全 | **需要支持**（refCount 用 atomic，mark 用 thread-safe 策略） |
| 4 | WeakRef | **需要支持**（不影响 mark-sweep，可检查存活） |

## ASAN 内存泄漏检测测试

在测试项目中增加独立的 ASAN 泄漏检测测试：

```
src/test/new/
  CMakeLists.txt
  test_main.cppm
  memory/
    ...
    leak_test.cppm               (ASAN 内存泄漏检测)
```

### CMakeLists.txt ASAN 支持

```cmake
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN AND MSVC)
  target_compile_options(${LX_NEW_TEST} PRIVATE /fsanitize=address)
  target_link_options(${LX_NEW_TEST} PRIVATE /fsanitize=address)
elseif(ENABLE_ASAN)
  target_compile_options(${LX_NEW_TEST} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(${LX_NEW_TEST} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
endif()
```

### 测试用例

```cppm
// test/memory/leak_test.cppm
// TL1: create + destroy scene graph → ASAN reports zero leaks
// TL2: allocate resources, add to root, removeFromRoot, tick() → zero leaks
// TL3: spill pool alloc + free → zero leaks
// TL4: raw buffer alloc + free → zero leaks
// TL5: cyclic refs (A→B→A) unrooted → GC collects both, zero leaks
// TL6: shared resource (mesh ref by 2 nodes) → ref counted correctly, no double-free
```

运行方式：`cmake -DENABLE_ASAN=ON .. && ninja LX_New_Test && ./LX_New_Test --leak`

## 待后续 spec 细化

1. **线程安全设计** — 确定 atomic refCount 的内存序、mark bitmap 的线程安全策略、ResourceManager/GameObjectManager 的并发访问边界
2. **WeakRef 设计** — WeakRef 如何与 mark-sweep 协调（不参与 marking，lock() 检查 alive 位），是否需要 ReferenceQueue 机制
3. **RawBuffer compact 算法** — 30% 碎片率阈值下的 compact 策略（compact 时是否暂停 GPU 上传、如何更新 rawOffset）

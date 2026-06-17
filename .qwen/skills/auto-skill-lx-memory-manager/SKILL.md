---
name: lx-memory-manager
description: LXEngine 内存管理器完整实现——ResourceManager (64-bit Handle + Table dispatch) + GameObjectManager (mark-sweep GC + SpillBlock overflow)
source: auto-skill
extracted_at: '2026-06-18T17:20:03.680Z'
---

# LXEngine 内存管理器

## 架构分层

```
ResourceManager（引用计数 + 64-bit Handle + Table 分发）
    ↑
GameObjectManager（mark-sweep GC + meta 管理 + handle cascade）
    ↑
Scene / Asset 创建（通过 manager 分配，不直接 new）
```

## 模块位置

| 组件 | 模块 | 目录 |
|---|---|---|
| RawBuffer | `LX_New_Common.Memory` (partition `:RawBuffer`) | `src/new_common/memory/raw_buffer.cppm` |
| SpillBlock/SpillPool | `LX_New_Common.Memory` (partition `:SpillBlock`) | `src/new_common/memory/spill_block.cppm` |
| ResourceHandle | `LX_New_Core.Resource` (partition `:ResourceHandle`) | `src/new_core/resource/resource_handle.cppm` |
| Types + ResourceTypeTableBase | `LX_New_Core.Resource` (partition `:Types`) | `src/new_core/resource/types.cppm` |
| TypedResourceTable | `LX_New_Core.Resource` (partition `:TypedResourceTable`) | `src/new_core/resource/typed_resource_table.cppm` |
| VariableResourceTable | `LX_New_Core.Resource` (partition `:VariableResourceTable`) | `src/new_core/resource/variable_resource_table.cppm` |
| ResourceManager | `LX_New_Core.Resource` (partition `:ResourceManager`) | `src/new_core/resource/resource_manager.cppm` |
| GameObjectMeta | `LX_New_Core.GameObject` (partition `:GameObjectMeta`) | `src/new_core/game_object/game_object_meta.cppm` |
| GameObjectManager | `LX_New_Core.GameObject` (partition `:GameObjectManager`) | `src/new_core/game_object/game_object_manager.cppm` |

**分区导入约定**：同模块分区用 `import :Types;`（短形式），跨模块用 `import LX_New_Core.Resource;`（全名，import umbrella）。

## ResourceHandle（64-bit，位移法）

```cpp
struct ResourceHandle {
    u64 raw = 0;
    // type_id(8) : index(24) : generation(16) : _reserved(16)

    constexpr ResourceHandle(MemoryTypeId t, MemoryIndex i, MemoryGeneration g) noexcept;
    [[nodiscard]] constexpr bool isValid() const noexcept;       // generation != 0
    static constexpr ResourceHandle invalid() noexcept;          // {0}
    auto operator<=>(const ResourceHandle&) const = default;    // needs <compare> in importers
};
```

## ResourceTypeTableBase — 类型擦除分发

定义在 `types.cppm` 中。用 `u64 rawHandle` 参数（不用 `ResourceHandle`）避免 Types ↔ ResourceHandle 循环依赖：

```cpp
class ResourceTypeTableBase {
public:
    virtual ~ResourceTypeTableBase() = default;
    virtual void release(u64 rawHandle) = 0;
};
```

`TypedResourceTable<T>` 和 `VariableResourceTable<MetaType>` 均继承此基类，override `release(u64)`。两者还新增 `addRef(ResourceHandle)` 支持共享资源（refCount > 1）。

## ResourceManager — Table 分发

```cpp
class ResourceManager {
    ResourceTypeTableBase* m_tables[256] = {};
    void registerTable(u8 typeId, ResourceTypeTableBase* table);
    void release(ResourceHandle handle);  // 按 type_id 分发到 m_tables
    void release(u64 rawHandle);
};
```

使用方式：外部创建 `TypedResourceTable<T>` 或 `VariableResourceTable<M>`，调 `setTypeId()`，然后 `rm.registerTable(typeId, &table)`。GC sweep 时 `m_resourceManager->release(rawHandle)` 自动分发。

## RawBuffer — 对齐内存分配器

`_aligned_malloc` 基址 256 字节对齐，first-fit + 合并相邻空闲块。move-only，析构自动 `_aligned_free`。

**注意**：`capacity()` 返回 `m_size`（已使用字节），非实际分配容量。`free()` 接收调用方传入的原始 size（非对齐后 size）。

## SpillPool<T,N> — 通用溢出池

从设计文档的 `SpillBlock` 提取为通用模板类。提供 `allocBlock`/`freeChain`/`append`/`itemCount`/`begin`/`end`。Iterator 只定义 `operator!=`（无 `operator==`）。

## GameObjectMeta（~56 bytes）

```cpp
struct GameObjectMeta {
    u8 marked : 1; u8 alive : 1; u8 _pad : 6;  // GC 状态
    u8  refCount; u32 refs[4]; u32 refSpillHead;       // inline refs ≤ 4 + spill
    u8  handleCount; u64 handles[4]; u32 handleSpillHead; // inline handles ≤ 4 + spill
};
```

## GameObjectManager — Mark-Sweep GC

```cpp
class GameObjectManager {
    std::vector<GameObjectMeta> m_metas;
    std::vector<u32> m_freeMetaList, m_roots;
    SpillPool<u32, 8> m_refPool;        // ref overflow
    SpillPool<u64, 8> m_handlePool;     // handle overflow
    ResourceManager* m_resourceManager; // nullable — null = no handle cascade

    explicit GameObjectManager(ResourceManager* rm = nullptr) noexcept;
};
```

### GC 算法

- **mark()**: 清除所有 `marked` → 从 `m_roots` 递归 `markRecursive`（遍历 inline refs[0..refCount) + spill ref 链）。`marked` 防止循环引用无限递归。
- **sweep()**: 未标记 alive meta → 释放 inline + spill handles 到 ResourceManager → 释放 spill 链 → `alive=0` → 放回 `m_freeMetaList`。
- **tick()**: mark() + sweep()。每帧调用。

### `kNoneChunk` 命名空间陷阱

`kNoneChunk` 定义在两处：
- `LX_New_Common::kNoneChunk`（`spill_block.cppm`）
- `LX_New_Core::kNoneChunk`（`resource/types.cppm`）

在 `LX_New_Core` 命名空间内同时 `using namespace LX_New_Common;` 会导致歧义。GameObject 模块中**不要**对两个命名空间同时使用 `using namespace`。

## 测试

全部测试通过 ASAN（零泄漏、零溢出）：

| 套件 | 文件 | 覆盖 |
|---|---|---|
| RawBuffer | `raw_buffer_test.cppm` | T1-T22：数据完整性、对齐、移动语义、合并、压力 |
| SpillPool | `spill_pool_test.cppm` | T1-T16：空状态、回收、多链、压力 |
| GameObjectManager | `game_object_manager_test.cppm` | T1-T16：meta 管理、setRefs/handles spill、mark、sweep、tick |
| GcIntegration | `gc_integration_test.cppm` | T1-T5：scene graph 生命周期、handle cascade |
| LeakTest | `leak_test.cppm` | TL1-TL4：ASAN 验证 |

## 关键决策

| 项 | 状态 |
|---|---|
| RawBuffer compact | 设计要求 >30% 碎片率触发，**尚未实现** |
| Partial GC | 暂不实现，每帧全量 GC |
| 线程安全 | 尚未实现（设计要求 atomic refCount） |
| WeakRef | 尚未实现 |

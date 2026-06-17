---
name: lx-memory-manager
description: LXEngine 内存管理器设计：ResourceManager (64-bit Handle + Table) + GameObjectManager (mark-sweep GC + SpillBlock)
source: auto-skill
extracted_at: '2026-06-17T13:16:08.983Z'
---

# LXEngine 内存管理器

## 架构分层

```
ResourceManager（引用计数 + 64-bit Handle）
    ↑
GameObjectManager（mark-sweep GC + meta 管理）
    ↑
Scene / Asset 创建（通过 manager 分配，不直接 new）
```

## ResourceHandle（64-bit packed）

```cpp
union ResourceHandle {
    u64 raw;
    struct {
        u64 type_id    : 8;   // 256 种资源类型
        u64 index      : 24;  // 每类型 16M 条目
        u64 generation : 16;  // 65536 世代
        u64 _reserved  : 16;  // 预留
    };
    bool isValid() const { return generation != 0; }
};
```

## ResourceManager — Table 结构

### 固定大小资源（TypedResourceTable）

meta 与 data **完全分离**，data 可直接上传 GPU：

```cpp
template<typename T>
struct TypedResourceTable {
    struct Meta { u16 refCount; u16 generation; };
    std::vector<Meta>  metas;     // CPU 端引用管理
    std::vector<T>     data;      // GPU 上传数据（连续）
    std::vector<u32>   freeList;  // 空闲槽位（LIFO）
};
```

### 可变长资源（VariableResourceTable）

```cpp
struct VariableResourceMeta { u16 refCount; u16 generation; u32 rawOffset; u32 rawSize; };
// meta[] + RawBuffer（大 buffer + free block list）
```

### RawBuffer — Free Block List

- first-fit 分配，可拆分为分配块 + 剩余块放回 free list
- 释放时合并相邻空闲块
- **碎片率 > 30% 时自动触发 compact**

## SpillBlock — 溢出池

inline 容量不足时（4 个 ref / 4 个 handle），用链表块存储：

```cpp
constexpr u32 kNoneChunk = 0xFFFFFFFF;
template<typename T, u32 N = 8>
struct SpillBlock {
    u8  count; u32 nextBlockIdx; T items[N];
};
```

meta 中存链头索引：`refSpillHead` / `handleSpillHead`。最大容量 2052 个 ref/handle。

## GameObjectMeta（56 bytes）

```cpp
struct GameObjectMeta {
    u8  marked : 1; u8  alive : 1; u8  _pad : 6;
    u8  refCount; u32 refs[4]; u32 refSpillHead;
    u8  handleCount; u64 handles[4]; u32 handleSpillHead;
};
```

## Mark-Sweep GC

- **每帧 tick()** 驱动，从 Root 出发遍历
- mark: 清除所有标记 → 从 root 递归标记可达对象（遍历 inline refs + spill refs）
- sweep: 未标记 meta → 释放 handles（通知 ResourceManager）→ 重置 meta → 放回 freeMetaList
- **循环引用安全**：cycle 中两个对象都未标记 → 一起回收

## GC tick 流程

```
每帧调用:
  mark()    → 清除所有标记 + 从 root 递归 markRecursive
  sweep()   → 释放未标记对象的 handles + 重置 meta

手动调用:
  removeFromRoot(scene_meta_idx)  → 场景卸载
  tick()                          → 下一帧回收
```

## MSVC 模块约束

由于 MSVC `CXX_SCAN_FOR_MODULES` 无法解析分区到分区的导入，以下类型必须**内联到 `memory.cppm`**（主 MIU）而非独立 `.cppm`：

- `TypedResourceTable<T>`、`VariableResourceTable<MetaType>`（模板类，跨分区依赖）
- `GameObjectMeta`、`GameObjectManager`（依赖 SpillBlock）

**重要**：`allocMeta()` 中必须显式设置 `refSpillHead = kNoneChunk` 和 `handleSpillHead = kNoneChunk`，因为 `GameObjectMeta{}` 默认初始化将它们设为 0 而非 `kNoneChunk`，会导致 mark 阶段遍历到无效索引触发断言失败。

## 测试项目

`src/test/new/` 使用 C++20 modules，支持 ASAN：

```powershell
# 常规测试
cmake .. -G Ninja && ninja LX_New_Test && ./LX_New_Test

# ASAN 泄漏检测
cmake .. -G Ninja -DENABLE_ASAN=ON && ninja LX_New_Test && ./LX_New_Test --leak
```

## 关键决策

| 项 | 决策 |
|---|---|
| RawBuffer compact | 碎片率 > 30% 自动触发 |
| Partial GC | 暂不实现，每帧全量 GC |
| 线程安全 | 需要（atomic refCount + thread-safe mark） |
| WeakRef | 需要（不影响 mark-sweep，lock() 检查存活） |

完整 spec: `docs/superpowers/specs/2026-06-17-memory-manager-design.md`

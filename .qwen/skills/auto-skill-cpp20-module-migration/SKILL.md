---
name: cpp20-module-migration
description: 将旧 C++ headers (.hpp/.cpp) 迁移到 new_common/new_core 的 C++20 modules (.cppm) 的约定和流程
source: auto-skill
extracted_at: '2026-06-17T10:00:57.924Z'
---

# 旧代码迁移到 C++20 Modules (new_common / new_core)

## 核心原则

- **旧代码不动**：在 `src/new_common/` 或 `src/new_core/` 下全新编写 `.cppm`，不修改 `src/core/` 下的旧 `.hpp/.cpp`
- CMake 通过 `file(GLOB_RECURSE ... *.cppm)` 自动发现新文件，无需手动修改 CMakeLists.txt

## 项目层级与模块前缀

| 层级 | 模块前缀 | 目录 |
|---|---|---|
| 基础库 | `LX_New_Common` | `src/new_common/` |
| 引擎核心 | `LX_New_Core` | `src/new_core/` |

## 模块命名约定

| 层级 | 格式 | 示例 |
|---|---|---|
| 库模块 | `LX_New_Common.Subsystem` | `LX_New_Common.Math` |
| 分区 | `LX_New_Common.Subsystem:PartitionName` | `LX_New_Common.Math:Vec` |
| 同子系统引用 | `import :PartitionName;` | `import :Vec;` |
| 跨子系统引用 | `import LX_New_Common.Subsystem;` | `import LX_New_Common.Platform;` |
| new_core 引用 new_common | `import LX_New_Common.XXX;` | `import LX_New_Common.Math;` |

## 标准 .cppm 文件结构

```cpp
module;                              // 全局模块片段
#include <...>                       // 标准库/system headers

export module LX_New_Common.Subsystem:PartitionName;  // 或无 :Partition 的主模块

import LX_New_Common.Platform;       // 跨子系统依赖
import :OtherPartition;              // 同子系统分区依赖

export namespace LX_New_Common {
    // 所有公开接口放在这里
}
```

## MSVC 模块约束（重要）

**MSVC 的 `CXX_SCAN_FOR_MODULES` 无法正确解析分区到分区的导入**（即同一个主模块下的 `:PartitionA` 导入 `:PartitionB`）。

**症状**：编译时报错 `cannot find module partition 'LX_New_Common.Memory:Types'` 或类似错误。

**解决方案**：将模板类或跨分区依赖的类型直接内联到主模块接口单元（MIU）中，而不是放在独立的 `.cppm` 分区文件里。

**示例 — 错误的分区拆分**：
```cpp
// types.cppm
export module LX_New_Common.Memory:Types;
export namespace LX_New_Common { using ResourceIndex = u32; }

// typed_resource_table.cppm
export module LX_New_Common.Memory:TypedResourceTable;
import LX_New_Common.Memory.Types;  // ← MSVC 无法解析这个
```

**示例 — 正确的内联做法**：
```cpp
// memory.cppm (主模块 MIU)
module;
#include <vector>
#include <algorithm>

export module LX_New_Common.Memory;

export import :ResourceHandle;
export import :RawBuffer;
export import :SpillBlock;

export namespace LX_New_Common {
// 内联模板类到 MIU 中，避免跨分区依赖
template<typename T>
class TypedResourceTable { /* ... */ };

template<typename MetaType>
class VariableResourceTable { /* ... */ };

struct GameObjectMeta { /* ... */ };
class GameObjectManager { /* ... */ };
} // namespace LX_New_Common
```

**已知需要内联到 `memory.cppm` 的类型**：
- `TypedResourceTable<T>`（模板，依赖 ResourceHandle）
- `VariableResourceTable<MetaType>`（模板，依赖 RawBuffer + ResourceHandle）
- `GameObjectMeta`（结构体，被 SpillBlock 引用）
- `GameObjectManager`（类，依赖 SpillBlock + GameObjectMeta）

**不需要内联的类型**（独立分区 OK）：
- `ResourceHandle`（纯 union，无跨分区依赖）
- `RawBuffer`（独立类，只依赖 Platform）
- `SpillBlock<T,N>`（模板，只依赖 Types，但 Types 可内联到 MIU）
- `ResourceManager`（简单占位，只依赖 umbrella）

## 分区类型

1. **独立分区**（如 `vec.cppm`, `mat.cppm`）：声明 `export module LX_New_Common.Math:Vec;`
2. **主模块/Umbrella**（如 `math.cppm`）：不写 `:Partition`，用 `export import :PartitionName;` 重新导出所有分区

```cpp
// math.cppm — 主模块 umbrella
module;
export module LX_New_Common.Math;
export import :Vec;
export import :Mat;
```

## new_core 目录约定

```
src/new_core/
  core.cppm                    (主模块 umbrella)
  resource/                    (底层资源 — GPU 数据，由 MemoryAllocator 创建)
  assets/                      (可定义/引用的资产)
    camera/
    light/
    mesh/
    material/
    render_object/
    render_feature/
  scene/                       (场景结构)
```

- **小类型聚合**：枚举、类型别名合并到 `types.cppm`
- **大概念独立**：有实质数据字段或计算逻辑的概念 → 一个概念一个 `.cppm`

## .hpp + .cpp 合并规则

旧代码中分离的声明(.hpp)和实现(.cpp)应合并到**同一个** `.cppm` 文件中：
- `.cpp` 中的 `namespace { ... }` 匿名辅助函数放入模块内部的匿名 namespace
- 不需要 module interface unit / implementation unit 分离（当前项目全部用单文件 .cppm）

## 类型别名迁移

旧代码使用的 `LX_core` namespace 和类型统一替换为：
- namespace: `LX_core` → `LX_New_Common`
- 基础类型: `float` → `f32`，`int32_t` → `i32`（来自 `LX_New_Common.Platform`）
- `Vec3T<T>` 在 mat.cppm 中已有 `template<typename T> using Vec3T = Vec3<T>;` 模板别名

## 构建验证

```powershell
cd build
cmake --build . --target LX_New_Common --config Debug
cmake --build . --target LX_New_Core --config Debug
```

新增 `.cppm` 文件会被 GLOB 自动捕获，无需修改 CMakeLists.txt。

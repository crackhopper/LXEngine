# String Interning

> 所有字符串键（binding 名、pass 名、shader 名）都被驻留成唯一的 `uint32_t` ID，比较与哈希都是 O(1) 整数操作。REQ-006 之后还支持**结构化** compose，把若干 `StringID` 组合成一个新的 `StringID`，用于 `PipelineKey` 这类树状身份。
>
> 权威 spec: `openspec/specs/string-interning/spec.md`
> 深度设计: `docs/design/GlobalStringTable.md`

## 核心抽象

- **`GlobalStringTable`** (`src/core/utils/string_table.hpp:44`) — 线程安全单例
  - `Intern(string_view) → StringID` — 叶子字符串驻留
  - `compose(TypeTag tag, span<const StringID> fields) → StringID` — 结构化组合
  - `decompose(StringID id) → optional<{tag, fields}>` — 反向解构
  - `toDebugString(StringID id) → string` — 递归展开成人类可读形式
  - `getName(id) → const string&` — 叶子字符串反查
- **`StringID`** — `uint32_t` 的强类型包装。从 `const char*` / `std::string` 隐式构造；整数相等对比；`StringID::Hash` 可作 `unordered_map` 的 hasher
- **`TypeTag`** (`src/core/utils/string_table.hpp:19`) — `uint8_t` 枚举，标记一个 `StringID` 是叶子 (`TypeTag::String`) 还是某类结构化 compose 结果 (`PipelineKey` / `ObjectRender` / `MaterialRender` / `VertexLayout` / ...)

## 典型用法

### 叶子驻留

```cpp
#include "core/utils/string_table.hpp"

using namespace LX_core;

StringID id1 = "baseColor";                       // 隐式构造 → Intern
StringID id2 = GlobalStringTable::get().Intern("baseColor");
assert(id1 == id2);                               // 同一个 uint32_t
```

### 结构化 compose（pipeline 身份构造）

```cpp
auto &tbl = GlobalStringTable::get();

// 叶子
StringID cullTag  = tbl.Intern("CullBack");
StringID blendTag = tbl.Intern("NoBlend");

// 一级 compose: RenderState
StringID renderStateSig = tbl.compose(TypeTag::RenderState, {cullTag, blendTag /*...*/});

// 二级 compose: 组合进 PipelineKey
StringID pipelineId = tbl.compose(TypeTag::PipelineKey, {objectSig, materialSig});

// 调试可读形式
std::cout << tbl.toDebugString(pipelineId) << '\n';
// → "PipelineKey(ObjectRender(MeshRender(VertexLayout(...), tri), Skn1), MaterialRender(...))"
```

### Scene 层 pass 常量

```cpp
#include "core/scene/pass.hpp"

auto item = scene->buildRenderingItem(LX_core::Pass_Forward);
// Pass_Forward 是 inline const StringID("Forward")
```

## 调用关系

```
Scene::buildRenderingItem(pass)
  │
  ├── 调用 IRenderable::getRenderSignature(pass)
  │     └── Mesh::getRenderSignature(pass) → compose(MeshRender, {layoutSig, topologySig})
  │     └── Skeleton::getRenderSignature()  → Intern("Skn1")（或 StringID{} 表示无骨骼）
  │
  ├── 调用 IMaterial::getRenderSignature(pass)
  │     └── MaterialTemplate::getRenderPassSignature(pass)
  │           └── RenderPassEntry::getRenderSignature()
  │                 ├── ShaderProgramSet::getRenderSignature()
  │                 └── RenderState::getRenderSignature()
  │
  └── PipelineKey::build(objSig, matSig) → compose(TypeTag::PipelineKey, {objSig, matSig})
```

## 注意事项

- **`Pass_Forward` 用 `inline const StringID` 而不是 `constexpr`**: `StringID` 的构造 intern 到全局表，有 side effect，不能 `constexpr`。
- **Compose 是顺序敏感的**: `compose(tag, [a, b]) ≠ compose(tag, [b, a])`。需要稳定顺序的地方（例如 shader variants）必须先 sort 再 compose。
- **叶子字符串不走 compose**: 比如 `topologySignature(TriangleList)` 直接返回 `Intern("tri")`，不要错写成 `compose(TypeTag::Topology, {})`（`TypeTag` 枚举里也故意没有 `Topology`）。
- **REQ-007 之前 vs 之后**: 之前 pipeline 身份通过 `getPipelineHash() → size_t` 的混 hash 通道；之后全面迁移到 `getRenderSignature(pass) → StringID`。查代码看到 `getPipelineHash()` 遗留只是过渡期产物，都应该视为等价已废弃。

## 延伸阅读

- `openspec/specs/string-interning/spec.md` — 所有 compose / decompose / toDebugString 的 normative 要求
- `openspec/specs/render-signature/spec.md` — `getRenderSignature(pass)` 在每类资源上的签名约定
- `docs/design/GlobalStringTable.md` — 实现细节（shared_mutex 并发、`m_composedEntries` 内存模型、TypeTag 命名策略）
- 归档: `openspec/changes/archive/2026-04-09-global-string-table/` — 基础版字符串表
- 归档: `openspec/changes/archive/2026-04-13-extend-string-table-compose/` — compose/decompose 扩展
- 归档: `openspec/changes/archive/2026-04-13-interning-pipeline-identity/` — 全面迁移 pipeline 身份到 interning

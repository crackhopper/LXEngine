# REQ-069-c: LXE Editor SceneRuntime 单文件拆分

> 2026-06-14 归档：本文件已并入 `REQ-076-a: Large File Decomposition Backlog`，不再作为单独 active 需求实施。以下内容保留为历史拆分草稿。

> 2026-06-02 新增：`REQ-069` 系列第三步，只拆分 `src/demos/lxe_editor/scene_runtime.cpp`。

## 背景

`src/demos/lxe_editor/scene_runtime.cpp` 当前约 2207 行。它同时处理 scene document 加载/保存、runtime scene 构建、editor/game camera 节点、资产根发现、材质 preset、材质参数 JSON、procedural material 状态、document capture、light/camera 状态采集和 runtime data。

其中有些职责已经有系统模块可以复用，例如：

| 已有模块 | 可能复用的职责 |
|---|---|
| `infra/scene_io/scene_document.*` | scene YAML document schema、load/save |
| `core/scene/Scene` / `SceneNode` | runtime scene graph |
| `core/scene/components/*` | camera/material/mesh/skeleton component 状态 |
| `infra/material_loader` | material asset loading |
| `core/asset/MaterialInstance` | material parameter/resource 状态 |

拆分时必须避免在 editor runtime 里复制 scene_io、material loader 或 component 自身已有的职责。

## 目标

1. 把 `scene_runtime.cpp` 拆成 document I/O 协调、runtime build、asset discovery、material surface、state capture 等语义清晰的小文件。
2. 明确 `SceneRuntime` 是 editor-facing facade，不是 scene_io 或 material loader 的第二套实现。
3. 保持 editor 加载、保存、material preset、procedural material、camera/light state capture 行为不变。
4. 产出 editor SceneRuntime 架构图和文件职责表。

## 需求

### R1: 先做职责审计和复用判断

实现前 SHALL 对 `scene_runtime.cpp` 中的职责块逐项审计：

| 当前职责 | 必须检查的已有模块 | 决策要求 |
|---|---|---|
| scene YAML load/save | `infra/scene_io/scene_document.*` | 不复制 YAML schema 逻辑 |
| runtime scene graph 创建 | `core/scene/Scene` / `SceneNode` | 只做 editor runtime 编排 |
| material asset loading | material loader / asset path helper | 不写第二套 material loader |
| material parameter JSON | `MaterialInstance` 和已有 helper | 尽量复用或集中 helper |
| camera/light state capture | component/light API | 不绕过 component 语义 |
| project asset roots | project/session/path helper | 抽单一 asset discovery helper |

审计结果 SHALL 写入实现说明或 source analysis。

### R2: 拆出 SceneRuntime facade 和 runtime data

`SceneRuntime` SHALL 保留 editor-facing facade 职责：

- `createEmptyScene()`
- `loadFromDocumentPath()`
- `saveToCurrentDocumentPath()`
- `scene()`
- editor/game camera 查询
- 节点材质查询/修改入口

内部 `SceneRuntimeData`、路径状态、runtime scene/document 组合可以保留在 facade 或拆到 internal header，但不得暴露不必要细节。

### R3: 拆出 document capture / restore helper

新增 helper 文件，负责：

- 从 runtime scene capture `SceneDocument`。
- capture camera/light/material/procedural state。
- document node lookup / normalization。

建议文件：

```text
src/demos/lxe_editor/scene_runtime_document_capture.hpp/.cpp
```

该 helper SHALL 使用 `infra/scene_io` 的 document 类型，不直接扩展 YAML 读写规则。

### R4: 拆出 asset discovery helper

新增 helper 文件，负责：

- project asset root discovery。
- runtime document path normalization。
- built-in primitive/material patch URI 推导。
- legacy editor helper node 判定。

建议文件：

```text
src/demos/lxe_editor/scene_runtime_assets.hpp/.cpp
```

如已有 project/session path helper 可复用，SHALL 优先复用。

### R5: 拆出 material surface helper

新增 helper 文件，负责：

- material preset discovery。
- node material URI 查询。
- node material base color 查询。
- material parameter/resource JSON 生成。
- procedural material enabled 状态查询/设置。

建议文件：

```text
src/demos/lxe_editor/scene_runtime_materials.hpp/.cpp
```

该 helper SHALL 复用 `MaterialInstance` 的参数/资源接口，不复制 material storage。

### R6: 保持 SceneRuntime public 行为稳定

拆分后 `SceneRuntime` public API 行为 SHALL 不变。保存/加载后的 scene document、editor camera、game camera、material overrides、environment、procedural material 状态应保持等价。

### R7: 产出 SceneRuntime 架构图和文件职责表

拆分完成后 SHALL 更新或新增 notes/source_analysis 页面，包含：

- `SceneRuntime` 与 `scene_io`、`Scene`、material loader、project session 的关系图。
- 每个新文件负责的功能。
- 每个文件复用哪些已有模块。
- 明确哪些职责不属于该文件。

建议图示内容：

```text
SceneRuntime
  -> scene_runtime_assets
  -> scene_runtime_document_capture -> infra/scene_io
  -> scene_runtime_materials -> MaterialInstance / material loader
  -> core Scene / SceneNode / components
```

## 测试

### T1: Build

- `ninja lxe_editor`

### T2: SceneRuntime regression

- `ninja test_scene_runtime`
- 或等价当前 scene runtime integration test target

### T3: Behavior checks

至少覆盖：

- scene load/save round-trip。
- editor camera 和 game camera 查询仍正确。
- material presets 仍包含当前候选。
- node material URI、base color、parameters 查询仍正确。
- procedural material 状态保存/恢复。
- environment 和 light/camera state capture 不丢字段。

## 修改范围

- `src/demos/lxe_editor/scene_runtime.cpp`
- `src/demos/lxe_editor/scene_runtime.hpp`
- `src/demos/lxe_editor/scene_runtime_assets.*`
- `src/demos/lxe_editor/scene_runtime_document_capture.*`
- `src/demos/lxe_editor/scene_runtime_materials.*`
- 相关 `CMakeLists.txt`
- `src/test/integration/test_scene_runtime.cpp` 或拆分后的 scene runtime tests
- 相关 notes/source_analysis

## 边界与约束

- 本 REQ 不修改 `infra/scene_io` schema；如果发现 schema 需要变化，应另开需求。
- 本 REQ 不新增 material loader 功能。
- 本 REQ 不拆 `editor_session.cpp` 或 `lxe_editor_commands.cpp`。
- 本 REQ 以等价搬迁为主，不改变 editor authoring 行为。

## 依赖

- `REQ-069-b: Core Editor Builtin Commands 单文件拆分`
- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/test-build-execution/spec.md`

## 下游工作

- 后续可单独拆分 `src/infra/scene_io/scene_document.cpp`。
- 后续可单独拆分 `src/demos/lxe_editor/editor_session.cpp`。

## 实施状态

2026-06-14 复核：保留 active，未完成。

`src/demos/lxe_editor/scene_runtime.cpp` 仍是 2000+ 行级单文件，加载、material override、environment/IBL、procedural runtime、selection/runtime state 等职责仍集中在 `SceneRuntime` 实现中。后续应先做搬迁和等价测试，再添加新行为。

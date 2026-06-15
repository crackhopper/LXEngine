# REQ-069-b: Core Editor Builtin Commands 单文件拆分

> 2026-06-14 归档：本文件已并入 `REQ-076-j: Large File Decomposition Backlog`，不再作为单独 active 需求实施。以下内容保留为历史拆分草稿。

> 2026-06-02 新增：`REQ-069` 系列第二步，只拆分 `src/core/editor/commands/builtin_commands.cpp`。本需求不实现新命令。

## 背景

`src/core/editor/commands/builtin_commands.cpp` 当前约 3603 行，是项目内最大的非第三方生产 `.cpp`。它同时包含 command registration、help/completion、scene/node/camera/light/material/selection/debug probe/undo-redo 命令、clipboard state、JSON formatter 和大量 parser/helper。

`REQ-068-a` 已经要求 command registration split，为 `realtime-render` 等后续命令提供更清晰落点。本需求只处理 core editor builtin commands 的等价拆分，避免继续向一个大文件追加命令。

## 目标

1. 把 `builtin_commands.cpp` 按命令域拆成多个语义明确的文件。
2. 拆分前审计已有 `CommandBus`、`EditorState`、scene/component API，优先复用已有 helper。
3. 保持当前 core command 行为、help、completion、undo/redo 语义不变。
4. 为每个命令域形成文件职责表，让我们能从文件名判断命令归属。

## 需求

### R1: 先做命令域和重复 helper 审计

实现前 SHALL 列出当前 `builtin_commands.cpp` 中的命令域、helper、state，并判断是否已有可复用模块：

| 当前职责 | 必须检查 |
|---|---|
| command registration | `CommandBus` 注册接口、现有 `builtin_commands.hpp` |
| scene/node lookup | `Scene`、`SceneNode`、`EditorState` 现有 API |
| camera/light 状态读写 | component/light 类型自身 API |
| material parameter JSON | `MaterialInstance`、已有 material helper/notes |
| undo/redo | `EditorState` / command result 现有语义 |
| completion/help | `CommandBus` completion/help 数据 |

不得为了拆文件复制 parser、JSON formatter 或 scene lookup 逻辑。共享 helper 应抽到命令域内部 helper 文件。

### R2: 拆成 core command 域文件

推荐结构：

```text
src/core/editor/commands/
  builtin_commands.hpp
  register_builtin_commands.cpp
  command_parse_helpers.hpp/.cpp
  command_json_helpers.hpp/.cpp
  scene_commands.cpp
  node_commands.cpp
  camera_commands.cpp
  light_commands.cpp
  material_commands.cpp
  selection_commands.cpp
  debug_probe_commands.cpp
  undo_redo_commands.cpp
```

职责：

| 文件 | 职责 |
|---|---|
| `register_builtin_commands.cpp` | 顶层注册顺序和跨域聚合 |
| `command_parse_helpers.*` | args parsing、path/field parsing、通用 token helper |
| `command_json_helpers.*` | command structured JSON formatter |
| `scene_commands.cpp` | scene-level command |
| `node_commands.cpp` | node create/delete/rename/duplicate/path command |
| `camera_commands.cpp` | camera component command |
| `light_commands.cpp` | directional/point/spot light command |
| `material_commands.cpp` | material URI、参数、资源 command |
| `selection_commands.cpp` | selection/multi-select command |
| `debug_probe_commands.cpp` | bounds、shadow projection、debug probe command |
| `undo_redo_commands.cpp` | undo/redo/history command |

### R3: 保持 public API 稳定

`builtin_commands.hpp` 对外入口 SHOULD 保持稳定。外部调用者不应需要知道命令拆到哪个 `.cpp`。

如果必须调整函数签名，必须说明原因，并更新所有调用点和测试。

### R4: Completion 和 help 不丢失

拆分后所有原有命令的：

- dispatch
- help text
- completion
- structured JSON 输出

SHALL 保持等价。

### R5: 产出 core command 架构图和文件职责表

拆分完成后 SHALL 更新或新增 notes/source_analysis 页面，说明：

- `CommandBus` 到各 command registration 文件的关系。
- 每个 command 域文件负责的命令集合。
- 共享 parse/json helper 的边界。
- 哪些逻辑没有重复实现，而是复用 scene/component/material API。

## 测试

### T1: Build

- `ninja lxe_editor`

### T2: CommandBus regression

- `ninja test_command_bus`
- 或等价当前 command bus integration test target

### T3: Behavior checks

至少覆盖：

- help/completion 命令仍列出原有 verbs。
- scene/node/camera/light/material/selection/debug/undo-redo 代表命令能 dispatch。
- undo/redo 语义保持。
- structured JSON 输出稳定。

## 修改范围

- `src/core/editor/commands/builtin_commands.cpp`
- `src/core/editor/commands/builtin_commands.hpp`
- `src/core/editor/commands/*commands.cpp`
- `src/core/editor/commands/*helpers.*`
- 相关 `CMakeLists.txt`
- `src/test/integration/test_command_bus.cpp` 或拆分后的 command tests
- 相关 notes/source_analysis

## 边界与约束

- 本 REQ 不新增 `realtime-render` 命令；该能力属于 `REQ-068-a`。
- 本 REQ 不拆 `src/demos/lxe_editor/lxe_editor_commands.cpp`；那属于后续单文件需求。
- 本 REQ 不改变 command 语义，只做等价拆分和 helper 去重。

## 依赖

- `REQ-068-a: Output Profiles 与 Realtime Render 生成`
- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/test-build-execution/spec.md`

## 下游工作

- `REQ-069-c` 拆分 `src/demos/lxe_editor/scene_runtime.cpp`。
- 后续需求可继续拆分 `src/demos/lxe_editor/lxe_editor_commands.cpp`。

## 实施状态

2026-06-14 复核：保留 active，未完成。

`src/core/editor/commands/builtin_commands.cpp` 仍是 3000+ 行级单文件。虽然部分 command surface 已向 demo/editor commands 拆分，但本 REQ 要求的 core command domain 文件拆分和等价测试尚未落地。

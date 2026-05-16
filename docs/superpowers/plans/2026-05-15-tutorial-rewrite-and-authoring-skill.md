# Tutorial Rewrite and Authoring Skill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the old flat PBR tutorial with five teacher-style tutorial series, rewrite `get-started`, add tutorial-support requirements, and create a reusable `tutorial-authoring` Codex skill.

**Architecture:** Keep tutorial prose under `notes/tutorial/` grouped by learning path, keep future engine work explicit in `notes/requirements/`, and keep authoring rules in a project-local skill. The docs describe current code as current reality and future registry-style workflows only when linked to active requirements.

**Tech Stack:** Markdown, MkDocs nav YAML, LXEngine notes conventions, project-local Codex skills.

---

## File Map

- Create `.codex/skills/tutorial-authoring/SKILL.md`: reusable authoring workflow and style guardrails.
- Create `notes/requirements/042-a-tutorial-light-asset-and-custom-light-registry.md`: future light asset/custom-light workflow requirement.
- Create `notes/requirements/042-b-tutorial-editor-extension-registry.md`: future editor toolbar/command extension workflow requirement.
- Create `notes/requirements/042-c-tutorial-custom-scene-node-registry.md`: future custom scene-node workflow requirement.
- Rewrite `notes/get-started.md`: tutorial entry and fastest editor path.
- Delete old tutorial pages:
  - `notes/tutorial/00-overview.md`
  - `notes/tutorial/01-pbr-theory.md`
  - `notes/tutorial/02-pbr-shader.md`
  - `notes/tutorial/03-material-loader.md`
  - `notes/tutorial/04-cube-geometry.md`
  - `notes/tutorial/05-app-main.md`
  - `notes/tutorial/06-build-and-run.md`
- Create `notes/tutorial/index.md`: tutorial hub.
- Create `notes/tutorial/start-project/*.md`: startup tutorial series.
- Create `notes/tutorial/custom-material/*.md`: material/Gooch tutorial series.
- Create `notes/tutorial/custom-light/*.md`: current and future light tutorial series.
- Create `notes/tutorial/extend-editor/*.md`: toolbar/command extension tutorial series.
- Create `notes/tutorial/extend-scene-node/*.md`: custom scene-node tutorial series.
- Modify `notes/nav.yml`: replace old Tutorial nav with grouped series.

## Task 1: Add Tutorial Authoring Skill

**Files:**
- Create: `.codex/skills/tutorial-authoring/SKILL.md`

- [ ] **Step 1: Create the skill directory**

Run:

```bash
mkdir -p .codex/skills/tutorial-authoring
```

Expected: directory exists.

- [ ] **Step 2: Write the skill**

Create `.codex/skills/tutorial-authoring/SKILL.md` with:

```markdown
---
name: tutorial-authoring
description: Write or update LXEngine newcomer tutorials under notes/tutorial/ and notes/get-started.md, including teacher-style explanations, current-code validation, future-capability requirement links, and tutorial navigation updates.
---

Use this skill when writing or editing LXEngine tutorials, rewriting `notes/get-started.md`, or adding requirements that support future-facing tutorial workflows.

## Inputs To Read

Always read:

- `AGENTS.md`
- `openspec/specs/notes-writing-style/spec.md`
- the target tutorial page or `notes/get-started.md`
- directly relevant current docs under `notes/concepts/`, `notes/subsystems/`, and `openspec/specs/`
- directly relevant code/assets before making current-behavior claims

## Core Rules

- Chinese-first prose.
- Use teacher voice: patient, concrete, incremental.
- Use first-person plural `我们`; avoid second-person narration.
- Start new concepts with a concrete analogy, then map the analogy to real code/assets.
- Explain each new concept in this order: why it exists, where it lives in this repo, how we use or change it.
- Use tables for parallel concepts, fields, files, APIs, and current-vs-future comparisons.
- Use annotated YAML for `.material` and `.scene.yaml` surfaces.
- Prefer `lxe_editor` workflows over standalone render test examples.
- Describe only current code as current reality.
- Mark future-facing tutorial paths explicitly and link an active `REQ-NNN` / `REQ-NNN-a` requirement.
- Do not link active paths for finished requirements; use `notes/requirements/finished/` only when historical context is truly needed.
- End tutorial pages with `我们已经学会了什么` and `下一步`.

## Workflow

1. Identify the tutorial series and page role.
2. Read current code/docs for all factual claims.
3. Separate current workflow from future workflow.
4. If a future workflow lacks a requirement, draft or request a requirement before teaching it as a path.
5. Write the page with analogy first, then repo mapping, then guided steps.
6. Update `notes/nav.yml` when adding/removing tutorial pages.
7. Run `scripts/notes/serve_site.sh --build`.
8. Report new pages, requirement links, and any existing site warnings that remain unrelated.

## Page Shape

Recommended structure:

```markdown
# <series/page title>

<one-paragraph analogy and goal>

## 我们先建立一个心智模型

## 当前仓库里它在哪里

## 一步一步操作

## 常见问题

## 我们已经学会了什么

## 下一步
```

Keep pages focused. Prefer multiple short tutorial pages over one long page that mixes concepts.
```

- [ ] **Step 3: Validate the skill metadata**

Run:

```bash
sed -n '1,80p' .codex/skills/tutorial-authoring/SKILL.md
```

Expected: frontmatter has `name: tutorial-authoring` and the description mentions tutorials, `notes/tutorial/`, `notes/get-started.md`, and requirements.

## Task 2: Add Future-Capability Requirements

**Files:**
- Create: `notes/requirements/042-a-tutorial-light-asset-and-custom-light-registry.md`
- Create: `notes/requirements/042-b-tutorial-editor-extension-registry.md`
- Create: `notes/requirements/042-c-tutorial-custom-scene-node-registry.md`

- [ ] **Step 1: Create REQ-042-a**

Write `notes/requirements/042-a-tutorial-light-asset-and-custom-light-registry.md`:

```markdown
# REQ-042-a: 教程支撑 — 光源资产与自定义光源注册入口

## 背景

`v0.1.0` 已经具备 `DirectionalLight`、`PointLight`、`SpotLight`、`SceneLightsUBO`、`SceneDocument.light.kind` 与 `lxe_editor` 作者入口。我们已经能教新人如何使用现有三类光源，也能解释新增 C++ 光源大致要碰哪些模块。

但如果教程要把“自定义灯光”讲成一条顺滑路径，当前代码仍然过于手工：新 light kind 需要同时改 core light 类型、scene document、scene runtime、Inspector、CommandBus、DebugDraw helper 和 shader binding 合同。教程可以讲原理，但不能把这种手工串改伪装成稳定扩展 API。

## 目标

1. 给教程提供一个可教学的 light preset / custom light 注册模型。
2. 让 `.scene.yaml` 或未来 light asset 能引用稳定的 light kind。
3. 让 editor 能通过注册表发现 light 类型、默认参数、Inspector 字段和 debug helper。
4. 保持现有 Directional / Point / Spot 行为不回归。

## 需求

### R1: Light kind 注册表

新增 light kind registry，至少能声明：

| 字段 | 含义 |
|---|---|
| `kind` | scene document / asset 中使用的稳定名字 |
| `displayName` | editor 显示名 |
| `defaults` | 创建新 light 时的默认参数 |
| `inspectorFields` | Inspector 可编辑字段 |
| `debugShape` | editor helper 使用的可视化形状 |

### R2: Scene document 与 registry 对齐

`SceneNodeDocument.light.kind` 读取时应通过 registry 校验。未知 kind 应给出稳定错误，错误中包含 scene path、kind 和可用 kind 列表。

### R3: Light preset asset 最小形状

定义 light preset YAML 的最小形状，例如：

```yaml
kind: Spot
color: [1.0, 0.95, 0.8]
intensity: 3.0
range: 8.0
innerConeDegrees: 20.0
outerConeDegrees: 35.0
```

### R4: Editor 创建入口复用 registry

Toolbar / CommandBus 创建 light 时不再硬编码三类光源列表，而是从 registry 读取可创建类型。

### R5: 测试覆盖

覆盖：

- 已注册 kind 可创建、保存、重新加载。
- 未知 kind 返回可诊断错误。
- registry 中的 Inspector 字段能生成编辑入口。
- Directional / Point / Spot 旧场景 round-trip 不回归。

## 修改范围

- `src/core/scene/light.*`
- `src/demos/lxe_editor/scene_document.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/core/editor/commands/builtin_commands.*`
- `src/core/editor/inspector_panel.*`
- `src/demos/lxe_editor/ui_overlay.*`
- 相关 tests

## 边界与约束

- 本 REQ 不实现新的光照公式。
- 本 REQ 不要求热加载 light preset。
- 本 REQ 不改变现有 `SceneLightsUBO` 上限。

## 依赖

- `REQ-041-g` 已完成的多类型光源底座。

## 后续工作

- 自定义 shader 对新 light kind 的消费合同。
- light preset 热重载。

## 实施状态

未开始。当前仅作为教程中“未来顺滑工作流”的支撑需求。
```

- [ ] **Step 2: Create REQ-042-b**

Write `notes/requirements/042-b-tutorial-editor-extension-registry.md`:

```markdown
# REQ-042-b: 教程支撑 — Editor toolbar 与 command 扩展注册入口

## 背景

当前 `lxe_editor` 已经采用 command-first 设计：toolbar、Inspector、API 和 MCP 诊断通道最终复用 `CommandBus`。这适合教学“按钮只是遥控器，命令总线才是线路”。

但新增 toolbar 按钮和 command 仍需要直接改 `UiOverlay`、`builtin_commands`、补全、测试和 API 状态。教程可以教当前手工扩展路径，但更适合新人学习的是稳定的扩展注册入口。

## 目标

1. 为教程提供可复用的 command 注册和 toolbar action 注册模型。
2. 让 toolbar 按钮通过 command schema 创建，而不是直接改 editor state。
3. 让补全、undo/redo、HTTP/WebSocket/MCP 复用同一份 command metadata。

## 需求

### R1: Command metadata

每个 command 声明：

| 字段 | 含义 |
|---|---|
| `verb` | 命令名字 |
| `summary` | 一句话说明 |
| `args` | 参数 schema |
| `undoable` | 是否支持 undo |
| `source` | builtin / extension |

### R2: Toolbar action metadata

toolbar action 声明：

| 字段 | 含义 |
|---|---|
| `id` | 稳定 action id |
| `label` | UI 显示名 |
| `icon` | 可选图标名 |
| `commandTemplate` | 点击时 dispatch 的命令模板 |
| `group` | toolbar 分组 |

### R3: Builtin commands 迁移到 metadata

现有核心命令继续保留 C++ handler，但补齐 metadata，使教程能通过同一接口解释命令、补全和 toolbar。

### R4: API 暴露 command / toolbar schema

HTTP/WebSocket/MCP 诊断通道可以查询 command 列表和 toolbar action 列表。

### R5: 测试覆盖

覆盖：

- metadata 与 handler 注册一致。
- toolbar action 点击只 dispatch command。
- command completion 使用 metadata。
- API 查询能看到新增 command。

## 修改范围

- `src/core/editor/command_bus.*`
- `src/core/editor/commands/builtin_commands.*`
- `src/demos/lxe_editor/ui_overlay.*`
- `src/demos/lxe_editor/lxe_editor_api_service.*`
- 相关 tests

## 边界与约束

- 本 REQ 不引入脚本插件系统。
- 本 REQ 不要求动态加载外部 binary。
- toolbar layout 仍由现有 ImGui UI 承载。

## 依赖

- `REQ-040-a`
- `REQ-041-b`
- `REQ-041-d`

## 后续工作

- command 权限 / HITL。
- 外部 extension package。

## 实施状态

未开始。当前仅作为教程中“未来顺滑工作流”的支撑需求。
```

- [ ] **Step 3: Create REQ-042-c**

Write `notes/requirements/042-c-tutorial-custom-scene-node-registry.md`:

```markdown
# REQ-042-c: 教程支撑 — 自定义场景节点类型注册入口

## 背景

当前 `SceneNode` 已经具备 transform、path、component、picking、DebugDraw helper、rename、duplicate、scene document capture/load 等基础。我们可以教学“一个节点要兼容编辑器操作，需要同时满足运行时、文档、命令和可视化合同”。

但新增一种节点语义仍然是手工接线：scene document 字段、runtime 构建、Inspector、CommandBus、DebugDraw、picking bounds、duplicate 语义都需要分散修改。教程可以讲清楚这些触点，但如果要让新人真正扩展节点，需要一个注册入口。

## 目标

1. 定义 custom scene node kind 的注册模型。
2. 让节点 kind 声明保存格式、runtime 构建、Inspector 字段、debug bounds/helper 和 duplicate 行为。
3. 让已有 primitive / camera / light 节点能逐步迁移到同一解释模型。

## 需求

### R1: Node kind metadata

每种节点 kind 声明：

| 字段 | 含义 |
|---|---|
| `kind` | scene document 中的稳定名字 |
| `displayName` | editor 显示名 |
| `components` | runtime 需要挂载的 component 类型 |
| `documentPayload` | 保存到 `.scene.yaml` 的负载 schema |
| `debugDraw` | 可选 debug helper |
| `boundsPolicy` | picking / selection bounds 来源 |
| `duplicatePolicy` | duplicate 时复制哪些 payload |

### R2: Scene document 使用 kind-aware payload

新增节点 kind 时，不应要求直接给 `SceneNodeDocument` 加一组专属字段。文档层需要能保存 kind-specific payload，并在加载时交给 registry 解释。

### R3: Runtime 构建通过 registry

`SceneRuntime` 构建节点时先识别 kind，再调用对应 factory。factory 返回 runtime node、component 和附加 scene-level 资源。

### R4: Editor 操作兼容

自定义节点必须兼容：

- select / deselect
- move / rotate / scale
- rename
- duplicate / copy / paste
- remove
- scene save / load
- DebugDraw helper
- API state summary

### R5: 测试覆盖

覆盖：

- 自定义节点 round-trip。
- duplicate 后 payload 独立。
- debug bounds 可用于 picking。
- 未知 kind 产生可诊断错误。

## 修改范围

- `src/demos/lxe_editor/scene_document.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/core/editor/commands/builtin_commands.*`
- `src/core/editor/inspector_panel.*`
- `src/core/editor/scene_tree_panel.*`
- `src/demos/lxe_editor/scene_interaction_controller.*`
- 相关 tests

## 边界与约束

- 本 REQ 不引入完整 ECS。
- 本 REQ 不要求脚本定义节点类型。
- 本 REQ 不改变 `SceneNode` 的 transform/path 基础语义。

## 依赖

- `REQ-037-a`
- `REQ-038-a`
- `REQ-041-f`

## 后续工作

- 脚本化 node kind。
- asset-driven node prefab。

## 实施状态

未开始。当前仅作为教程中“未来顺滑工作流”的支撑需求。
```

- [ ] **Step 4: Verify requirement list generation**

Run:

```bash
scripts/notes/serve_site.sh --build
```

Expected: build completes; generated requirements index includes `042-a`, `042-b`, and `042-c`.

## Task 3: Rewrite GetStarted and Tutorial Hub

**Files:**
- Modify: `notes/get-started.md`
- Create: `notes/tutorial/index.md`

- [ ] **Step 1: Rewrite `notes/get-started.md`**

Replace the file with:

```markdown
# GetStarted

我们先把 LXEngine 当成一间正在搭建的教学工作室：底层已经有 Vulkan 渲染器、材质系统、场景对象和一个可交互的 `lxe_editor`；我们学习它时，不需要一开始就拆开所有机械结构，而是先学会开门、开灯、摆一个物体，再逐步理解背后的系统。

## 最短路径

| 目标 | 命令 |
|---|---|
| 配置工程 | `mkdir -p build && cd build && cmake .. -G Ninja` |
| 验证 shader 编译 | `ninja test_shader_compiler && ./src/test/test_shader_compiler` |
| 构建编辑器 | `ninja lxe_editor` |
| 启动编辑器 | `./src/demos/lxe_editor/lxe_editor` |

如果我们只想确认机器环境是否正常，先跑 `test_shader_compiler`。它不需要窗口和 GPU 交互，能最快暴露 `shaderc`、`glslc`、SPIRV-Cross 和 shader 文件路径问题。

## 我们现在主要使用哪个入口

当前新人教程默认以 `lxe_editor` 为主入口。旧的 `test_render_triangle` 仍然适合做底层 smoke test，但它不是主教学路径。

| 入口 | 适合做什么 |
|---|---|
| `test_shader_compiler` | 验证 shader 编译和反射链路 |
| `test_render_triangle` | 验证窗口、Vulkan backend、最小 draw loop |
| `lxe_editor` | 学习场景、材质、光源、编辑器命令和保存/加载 |

## 环境准备

Linux 上至少需要：

- C++20 编译器
- `cmake` 3.16+
- `ninja`
- Vulkan SDK 或系统 Vulkan 开发环境
- `glslc`
- `shaderc`

先检查：

```bash
cmake --version
ninja --version
glslc --version
```

如果 CMake 找不到 Vulkan 或 shaderc，先修本机依赖，不要急着改源码。

## 教程地图

| 系列 | 我们学什么 | 入口 |
|---|---|---|
| 启动项目 | 安装、构建、启动 editor、加载和保存场景 | [Tutorial / 启动项目](tutorial/start-project/index.md) |
| 自定义材质 | `.material`、shader、参数、Gooch shader、editor 验证 | [Tutorial / 自定义材质](tutorial/custom-material/index.md) |
| 自定义灯光 | 当前 light 底座、scene YAML、未来 light asset / custom light 扩展 | [Tutorial / 自定义灯光](tutorial/custom-light/index.md) |
| 扩展编辑器 | toolbar 按钮、command、undo/API/MCP 复用 | [Tutorial / 扩展编辑器](tutorial/extend-editor/index.md) |
| 扩展场景节点 | 新 node kind、保存/加载、DebugDraw、兼容 editor 操作 | [Tutorial / 扩展场景节点](tutorial/extend-scene-node/index.md) |

## 当前能力和未来能力

有些教程会讲“今天就能做”的路径，有些会讲“未来应该这样做”的顺滑路径。我们用这个规则区分：

| 标记 | 含义 |
|---|---|
| 当前可用 | 已经能在当前代码中验证 |
| 未来工作流 | 教程会讲设计方向，但必须链接到 `notes/requirements/` 下的 active REQ |

目前 3-5 系列里涉及的未来工作流会链接到：

- `REQ-042-a`：光源资产与自定义光源注册入口
- `REQ-042-b`：Editor toolbar 与 command 扩展注册入口
- `REQ-042-c`：自定义场景节点类型注册入口

## 继续阅读

- [Tutorial 总览](tutorial/index.md)
- [v0.1.0 CHANGELOG](releases/v0.1.0/CHANGELOG.md)
- [Scene 子系统](subsystems/scene.md)
- [Material System 子系统](subsystems/material-system.md)
```

- [ ] **Step 2: Create `notes/tutorial/index.md`**

Write:

```markdown
# Tutorial

我们把这组教程当成一门循序渐进的课程：先会启动项目，再学会改变物体表面，接着理解光源、编辑器扩展和场景节点扩展。每个系列都从一个类比开始，再落到当前仓库中的真实文件、命令和调试方法。

## 推荐学习顺序

| 顺序 | 系列 | 学习目标 |
|---|---|---|
| 1 | [启动项目](start-project/index.md) | 把工程构建起来，打开 editor，加载和保存场景 |
| 2 | [自定义材质](custom-material/index.md) | 理解材质系统，并写一个 Gooch shader |
| 3 | [自定义灯光](custom-light/index.md) | 理解当前 light 底座和未来 custom light 工作流 |
| 4 | [扩展编辑器](extend-editor/index.md) | 理解 toolbar 与 command bus 如何配合 |
| 5 | [扩展场景节点](extend-scene-node/index.md) | 理解一种新节点如何兼容 editor 全流程 |

## 当前能力与未来能力

前两个系列尽量使用当前已经落地的能力。后三个系列会同时讲当前手工路径和未来更顺滑的扩展路径；凡是未来路径，页面会明确链接到对应 requirement。

| 未来路径 | Requirement |
|---|---|
| 光源资产与自定义光源注册 | [REQ-042-a](../requirements/042-a-tutorial-light-asset-and-custom-light-registry.md) |
| toolbar / command 扩展注册 | [REQ-042-b](../requirements/042-b-tutorial-editor-extension-registry.md) |
| 自定义场景节点注册 | [REQ-042-c](../requirements/042-c-tutorial-custom-scene-node-registry.md) |

## 学习方法

每一章都按同一个节奏走：

1. 先用类比建立心智模型。
2. 再指出当前仓库里的真实文件。
3. 然后一步一步操作。
4. 最后用排错清单把常见失败收束起来。

我们不要求一开始理解所有底层 Vulkan 细节。先把一条链路跑通，再回头读 [概念](../concepts/scene/index.md) 和 [子系统设计](../subsystems/index.md)，会更稳。
```

## Task 4: Replace Tutorial Series Content

**Files:**
- Delete old flat tutorial pages.
- Create all tutorial files listed in the File Map.

- [ ] **Step 1: Remove old flat PBR pages**

Run:

```bash
rm notes/tutorial/00-overview.md \
   notes/tutorial/01-pbr-theory.md \
   notes/tutorial/02-pbr-shader.md \
   notes/tutorial/03-material-loader.md \
   notes/tutorial/04-cube-geometry.md \
   notes/tutorial/05-app-main.md \
   notes/tutorial/06-build-and-run.md
```

Expected: old flat pages no longer exist.

- [ ] **Step 2: Create tutorial directories**

Run:

```bash
mkdir -p notes/tutorial/start-project \
         notes/tutorial/custom-material \
         notes/tutorial/custom-light \
         notes/tutorial/extend-editor \
         notes/tutorial/extend-scene-node
```

Expected: five series directories exist.

- [ ] **Step 3: Write Start Project series**

Create:

```text
notes/tutorial/start-project/index.md
notes/tutorial/start-project/01-environment-and-build.md
notes/tutorial/start-project/02-start-editor.md
notes/tutorial/start-project/03-load-and-save-scene.md
notes/tutorial/start-project/04-basic-authoring.md
notes/tutorial/start-project/05-troubleshooting.md
```

Content requirements:

- Use the classroom/workshop analogy from `get-started`.
- Use real commands:
  - `mkdir -p build`
  - `cmake .. -G Ninja`
  - `ninja test_shader_compiler`
  - `ninja lxe_editor`
  - `./src/demos/lxe_editor/lxe_editor`
- Explain `assets/scenes/lxe_editor.scene.yaml`, `data/scenes/`, `scene list`, `scene load`, `scene save`.
- Explain basic editor actions: toolbar create, select, transform, duplicate, preview, save.
- Use final sections `我们已经学会了什么` and `下一步`.

- [ ] **Step 4: Write Custom Material series**

Create:

```text
notes/tutorial/custom-material/index.md
notes/tutorial/custom-material/01-material-building-blocks.md
notes/tutorial/custom-material/02-material-yaml-and-shader-contract.md
notes/tutorial/custom-material/03-start-from-rtr-template.md
notes/tutorial/custom-material/04-write-gooch-shader.md
notes/tutorial/custom-material/05-verify-in-editor.md
notes/tutorial/custom-material/06-debug-material-problems.md
```

Content requirements:

- Use material-as-recipe analogy.
- Ground in current files:
  - `assets/materials/rtr_experiment_template.material`
  - `assets/shaders/glsl/rtr_experiment_template.vert`
  - `assets/shaders/glsl/rtr_experiment_template.frag`
  - `assets/shaders/glsl/scene_lights_ubo.glsl`
  - `src/infra/material_loader/generic_material_loader.*`
  - `src/core/asset/material_template.hpp`
  - `src/core/asset/material_instance.hpp`
- Include annotated `.material` YAML.
- Include a simple Gooch fragment shader explanation.
- Explain editor verification through Inspector material URI and node-level parameters.
- Include debug matrix: shader compile, reflection, system-owned binding, parameter typo, black object, no light.

- [ ] **Step 5: Write Custom Light series**

Create:

```text
notes/tutorial/custom-light/index.md
notes/tutorial/custom-light/01-current-light-model.md
notes/tutorial/custom-light/02-define-lights-in-scene-yaml.md
notes/tutorial/custom-light/03-extend-light-in-cpp.md
notes/tutorial/custom-light/04-future-light-assets.md
notes/tutorial/custom-light/05-debug-light-problems.md
```

Content requirements:

- Use lamp/fixture analogy.
- Ground current capability in:
  - `src/core/scene/light.hpp`
  - `src/core/scene/scene.cpp`
  - `src/demos/lxe_editor/scene_document.hpp`
  - `src/demos/lxe_editor/scene_runtime.cpp`
  - `assets/shaders/glsl/scene_lights_ubo.glsl`
- Include annotated `.scene.yaml` examples for Directional / Point / Spot.
- Mark C++ custom-light extension as current hand-wired path.
- Mark light preset/custom registry as future workflow and link `REQ-042-a`.

- [ ] **Step 6: Write Extend Editor series**

Create:

```text
notes/tutorial/extend-editor/index.md
notes/tutorial/extend-editor/01-command-first-editor.md
notes/tutorial/extend-editor/02-add-a-command.md
notes/tutorial/extend-editor/03-add-a-toolbar-button.md
notes/tutorial/extend-editor/04-connect-undo-api-and-mcp.md
notes/tutorial/extend-editor/05-debug-editor-extension.md
```

Content requirements:

- Use remote-control/wiring analogy.
- Ground current capability in:
  - `src/core/editor/command_bus.*`
  - `src/core/editor/commands/builtin_commands.*`
  - `src/demos/lxe_editor/ui_overlay.*`
  - `src/demos/lxe_editor/lxe_editor_api_service.*`
- Teach current hand-wired extension path.
- Mark metadata/registry-driven extension as future workflow and link `REQ-042-b`.

- [ ] **Step 7: Write Extend Scene Node series**

Create:

```text
notes/tutorial/extend-scene-node/index.md
notes/tutorial/extend-scene-node/01-scene-node-contract.md
notes/tutorial/extend-scene-node/02-design-a-new-node-kind.md
notes/tutorial/extend-scene-node/03-save-load-and-command-support.md
notes/tutorial/extend-scene-node/04-debug-draw-and-picking.md
notes/tutorial/extend-scene-node/05-compatible-editor-operations.md
```

Content requirements:

- Use actor-on-stage analogy.
- Ground current capability in:
  - `src/core/scene/object.*`
  - `src/core/scene/component.*`
  - `src/demos/lxe_editor/scene_document.*`
  - `src/demos/lxe_editor/scene_runtime.*`
  - `src/core/debug_draw/debug_draw.*`
  - `src/core/editor/commands/builtin_commands.*`
- Explain current hand-wired node-kind path.
- Mark registry-driven custom node kind as future workflow and link `REQ-042-c`.

## Task 5: Update Navigation

**Files:**
- Modify: `notes/nav.yml`

- [ ] **Step 1: Replace Tutorial nav**

Replace the current Tutorial block with:

```yaml
  - Tutorial:
      - 总览: tutorial/index.md
      - 启动项目:
          - tutorial/start-project/index.md
          - 环境与构建: tutorial/start-project/01-environment-and-build.md
          - 启动 editor: tutorial/start-project/02-start-editor.md
          - 加载与保存场景: tutorial/start-project/03-load-and-save-scene.md
          - 基础场景编辑: tutorial/start-project/04-basic-authoring.md
          - 启动排错: tutorial/start-project/05-troubleshooting.md
      - 自定义材质:
          - tutorial/custom-material/index.md
          - 材质积木: tutorial/custom-material/01-material-building-blocks.md
          - YAML 与 Shader 合同: tutorial/custom-material/02-material-yaml-and-shader-contract.md
          - 从 RTR 模板开始: tutorial/custom-material/03-start-from-rtr-template.md
          - Gooch Shader: tutorial/custom-material/04-write-gooch-shader.md
          - 在 editor 中验证: tutorial/custom-material/05-verify-in-editor.md
          - 材质排错: tutorial/custom-material/06-debug-material-problems.md
      - 自定义灯光:
          - tutorial/custom-light/index.md
          - 当前光源模型: tutorial/custom-light/01-current-light-model.md
          - Scene YAML 定义光源: tutorial/custom-light/02-define-lights-in-scene-yaml.md
          - C++ 扩展光源: tutorial/custom-light/03-extend-light-in-cpp.md
          - 未来光源资产: tutorial/custom-light/04-future-light-assets.md
          - 光源排错: tutorial/custom-light/05-debug-light-problems.md
      - 扩展编辑器:
          - tutorial/extend-editor/index.md
          - Command-first 编辑器: tutorial/extend-editor/01-command-first-editor.md
          - 增加 command: tutorial/extend-editor/02-add-a-command.md
          - 增加 toolbar 按钮: tutorial/extend-editor/03-add-a-toolbar-button.md
          - 连接 Undo/API/MCP: tutorial/extend-editor/04-connect-undo-api-and-mcp.md
          - 编辑器扩展排错: tutorial/extend-editor/05-debug-editor-extension.md
      - 扩展场景节点:
          - tutorial/extend-scene-node/index.md
          - SceneNode 合同: tutorial/extend-scene-node/01-scene-node-contract.md
          - 设计新节点类型: tutorial/extend-scene-node/02-design-a-new-node-kind.md
          - 保存、加载与命令: tutorial/extend-scene-node/03-save-load-and-command-support.md
          - DebugDraw 与 Picking: tutorial/extend-scene-node/04-debug-draw-and-picking.md
          - 兼容编辑器操作: tutorial/extend-scene-node/05-compatible-editor-operations.md
```

- [ ] **Step 2: Verify no stale old tutorial nav remains**

Run:

```bash
rg -n "00-overview|01-pbr-theory|02-pbr-shader|03-material-loader|04-cube-geometry|05-app-main|06-build-and-run" notes/nav.yml notes/tutorial
```

Expected: no matches.

## Task 6: Build and Final Validation

**Files:**
- No planned edits unless validation finds a local doc-link issue introduced by this change.

- [ ] **Step 1: Build notes**

Run:

```bash
scripts/notes/serve_site.sh --build
```

Expected: command exits 0. Existing unrelated link warnings may remain.

- [ ] **Step 2: Check generated nav**

Run:

```bash
rg -n "启动项目|自定义材质|自定义灯光|扩展编辑器|扩展场景节点|REQ-042-a|REQ-042-b|REQ-042-c" mkdocs.gen.yml notes/requirements/index.md
```

Expected: all five tutorial groups and all three requirements are present.

- [ ] **Step 3: Check future links**

Run:

```bash
rg -n "REQ-042-a|REQ-042-b|REQ-042-c" notes/tutorial notes/get-started.md
```

Expected:

- Custom light pages link `REQ-042-a`.
- Extend editor pages link `REQ-042-b`.
- Extend scene node pages link `REQ-042-c`.

- [ ] **Step 4: Check teacher voice**

Run:

```bash
rg -n "你可以|你会|TODO|TBD|待定" notes/get-started.md notes/tutorial .codex/skills/tutorial-authoring/SKILL.md
```

Expected: no `TODO`, `TBD`, or `待定`; any `你可以` / `你会` occurrences should be inside quoted examples only. Rewrite otherwise.

- [ ] **Step 5: Review final diff**

Run:

```bash
git diff --stat
git diff -- notes/get-started.md notes/tutorial notes/requirements notes/nav.yml .codex/skills/tutorial-authoring/SKILL.md
```

Expected: diff only covers tutorial rewrite, requirements, skill, and nav.

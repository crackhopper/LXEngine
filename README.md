# LXEngine

LXEngine 是一个以 Vulkan 渲染器为起点、正在走向 AI Native 小型游戏引擎的 C++20 工程。当前 `0.2.0-pre` 基线关注三件事：

1. 把 realtime editor、RenderPathGraph / FrameGraph、PBR + IBL、offline compute renderer 这些渲染主干站稳。
2. 让 scene、material、render path、pipeline、resource table 这些核心概念有清晰的代码边界。
3. 给人类开发者和 agent 都留下可观察、可验证、可扩展的入口。

## 当前主入口

| 入口 | 路径 / target | 用途 |
|---|---|---|
| Realtime editor | `src/editor/`, target `lxe_editor` | 打开 project / scene、调材质和光源、保存场景、通过 CommandBus/API/recording 观察状态 |
| Offline renderer | `src/tools/lxe_offline_render/`, target `lxe_offline_render` | 无窗口读取 `.scene.yaml`，运行 headless Vulkan compute renderer，输出 EXR/PNG/JSON/raw |
| Shader smoke | target `test_shader_compiler` | 快速验证 shaderc、glslc、SPIRV-Cross 和 shader 路径 |
| Integration tests | `src/test/integration/`, target `BuildTest` | 覆盖 shader、material、scene、pipeline、resource、editor 等集成路径 |
| Agent/remote manager | `src/tools/lxe_manager/` | MCP 管理服务，用于启动 editor、构建、日志、录制、远端诊断 |

`Renderer` 仍是 CMake project 名称和历史 bootstrap/env-probe 语境，不是当前主要交互入口。当前主要交互入口是 `lxe_editor`。

## 代码结构

| 路径 | 当前职责 |
|---|---|
| `src/core/` | 平台无关核心：scene、asset、material、frame graph、pipeline identity、RHI 接口、offline 数据结构 |
| `src/infra/` | 工具与加载层：window、ImGui、shader compiler/reflection、mesh/texture/material loader、scene I/O、offline scene compiler |
| `src/backend/vulkan/` | Vulkan 后端：device、swapchain、resource upload、attachments、descriptor、pipeline、command buffer、present、offline compute |
| `src/editor/` | 当前交互 editor：project/session、scene runtime、UI panels、CommandBus、HTTP/WebSocket API、recording |
| `src/tools/` | 独立工具：offline renderer、EXR compare、image probe、glTF/material 转换、PBRT scene 转换、lxe_manager、assets-downloader |
| `src/test/` | 集成测试和 new-track 测试入口 |
| `src/new_common/`, `src/new_core/` | C++20 module rewrite 试验线，默认不参与普通开发，需显式 `LX_BUILD_NEW_TRACK=ON` |
| `assets/` | runtime assets：shaders、materials、models、textures、env、scenes、render_paths |
| `notes/` | 当前人类可读文档站点 |
| `docs/superpowers/` | Superpowers specs/plans，记录当前设计讨论和执行计划 |

更细的目录说明见 [项目目录结构](notes/concepts-design/project-layout.md)。

## 快速构建

Linux / Ninja 最短路径：

```bash
cmake -S . -B build -G Ninja
cmake --build build --target test_shader_compiler
./build/src/test/test_shader_compiler
cmake --build build --target lxe_editor -j2
./build/src/editor/lxe_editor
```

Offline renderer smoke：

```bash
cmake --build build --target CompileShaders lxe_offline_render test_vulkan_offline_renderer -j2
ctest --test-dir build --output-on-failure -R 'test_vulkan_offline_renderer'
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml \
  --profile preview \
  --samples 1 \
  --width 64 \
  --height 64 \
  --max-bounce 1 \
  --out artifacts/offline/smoke
```

常用自动化验证：

```bash
cmake --build build --target BuildTest
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

依赖基线：

- CMake 3.16+
- C++20 编译器
- Ninja（Linux 推荐）
- Vulkan SDK 或系统 Vulkan 开发环境
- `glslc`、shaderc、SPIRV-Cross
- SDL3 或 GLFW window backend

## 文档入口

所有当前人类可读文档都从 `notes/` 进入。推荐启动本地站点，而不是只读散落的 Markdown：

```bash
scripts/notes/serve_site.sh
```

常用变体：

```bash
scripts/notes/serve_site.sh --foreground
scripts/notes/serve_site.sh --build
```

| 想了解 | 当前入口 |
|---|---|
| 项目速览 | [notes/README.md](notes/README.md) |
| 第一次构建和启动 | [notes/get-started.md](notes/get-started.md) |
| 架构总览 | [notes/concepts-design/architecture.md](notes/concepts-design/architecture.md) |
| 目录职责 | [notes/concepts-design/project-layout.md](notes/concepts-design/project-layout.md) |
| 场景系统 | [notes/scene-system/index.md](notes/scene-system/index.md) |
| 材质系统 | [notes/concepts/material/index.md](notes/concepts/material/index.md) |
| 渲染管线 | [notes/concepts-design/rendering-pipeline/index.md](notes/concepts-design/rendering-pipeline/index.md) |
| 子系统维护视角 | [notes/subsystems/index.md](notes/subsystems/index.md) |
| 源码分析 | [notes/source_analysis/index.md](notes/source_analysis/index.md) |
| Roadmap | [notes/roadmaps/README.md](notes/roadmaps/README.md) |
| 当前活跃需求 | [notes/requirements/index.md](notes/requirements/index.md) |
| 工具链说明 | [notes/tools/index.md](notes/tools/index.md) |

## 当前事实源

旧 plan 或历史 requirement 只代表当时上下文，不代表当前权威事实源。

当代码、文档和历史计划不一致时，按这个顺序判断：

1. `src/`、`assets/`、`CMakeLists.txt` 和当前测试。
2. `docs/superpowers/specs/` 中仍有效的设计 spec。
3. `notes/requirements/` 中 active requirement。
4. `notes/concepts-design/`、`notes/concepts/`、`notes/scene-system/`、`notes/subsystems/`。
5. `notes/source_analysis/`、`notes/roadmaps/`。
6. `notes/requirements/finished/`、历史 plans、临时笔记。

Roadmap 说明方向，不能单独证明能力已经实现。是否已经实现，要回到代码、当前设计 spec、active requirement 和验证命令。

## 开发工作流

当前命令入口位于 `.codex/commands/`：

| 命令 | 用途 |
|---|---|
| `/draft-req` | 把新想法整理成 active requirement |
| `/finish-req` | 校验 requirement、补齐状态、归档完成项 |
| `/update-notes` | 按当前代码更新 notes |
| `/refresh-notes` | 重启本地 notes 站点 |
| `/sync-design-docs` | 同步设计索引 |
| `/init-notes` | 初始化 notes 结构 |

典型路径：

```text
idea / problem
  -> /draft-req      optional
  -> Superpowers brainstorming/design
  -> Superpowers implementation plan
  -> implementation + verification
  -> /finish-req
  -> /update-notes
  -> /refresh-notes
  -> git commit
```

需求文件在 `notes/requirements/`，按 `NNN-*.md` 编号顺序推进。一个 REQ 文件只覆盖一个连续实施周期；如果新需求让旧需求的一部分后置，先拆分旧需求，并优先使用 `NNN-a` / `NNN-b` 后缀族，避免后续编号连锁变化。

## Coding agent 入口

- [AGENTS.md](AGENTS.md) 是 coding agent 的唯一权威入口。
- [CLAUDE.md](CLAUDE.md) 和 `.cursorrules` 只应指向 `AGENTS.md`，不维护独立项目记忆。

改代码前先读 `AGENTS.md`、相关 notes 设计页和当前 Superpowers spec；改 notes 前遵守 `.codex/skills/writing-notes/SKILL.md`。

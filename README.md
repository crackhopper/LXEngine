# LXEngine

基于 Vulkan 的 C++20 模块化 3D 渲染器，正在往 AI-Native 小型游戏引擎方向推进。仓库围绕三层结构组织：

- `src/core/` — 平台无关接口、数学、资源类型、场景图、runtime 编排
- `src/infra/` — 窗口、资源加载、shader 编译、GUI 等基础设施实现
- `src/backend/vulkan/` — Vulkan 渲染后端

交互式 demo 入口：`src/demos/scene_viewer/`。集成测试：`src/test/integration/`。

## 文档在哪里

**所有人类可读文档都在 [`notes/`](notes/)**（原 `docs/` 与 `faq/` 已并入）。

| 读什么 | 去哪里 |
|---|---|
| 项目速览 | [`notes/README.md`](notes/README.md) |
| 新手上手 | [`notes/get-started.md`](notes/get-started.md) |
| 目录职责 | [`notes/project-layout.md`](notes/project-layout.md) |
| 三层架构 + 数据流 | [`notes/architecture.md`](notes/architecture.md) |
| 子系统设计（维护者视角） | [`notes/subsystems/`](notes/subsystems/) |
| 使用者视角概念 | [`notes/concepts/`](notes/concepts/) |
| 路线图 | [`notes/roadmaps/main-roadmap/README.md`](notes/roadmaps/main-roadmap/README.md) |
| 当前活跃需求 | [`notes/requirements/`](notes/requirements/) |
| FAQ / 排错 | [`notes/faq/README.md`](notes/faq/README.md) |
| 行为契约（权威） | `openspec/specs/<capability>/spec.md` |

### 本地启动 notes 站点（推荐）

notes 按 MkDocs 站点组织，本地预览比直接读 markdown 体验好很多（左侧导航、站内搜索、Mermaid 图示、LaTeX 公式渲染）：

```bash
scripts/notes/serve_site.sh
```

终端会打印本地访问地址（通常 <http://127.0.0.1:8000>）。脚本行为：

1. 生成 `mkdocs.gen.yml`（读取 `notes/nav.yml` + `mkdocs.yml`）
2. 停旧进程
3. 后台启动 `mkdocs serve`

常用变体：

```bash
scripts/notes/serve_site.sh --foreground   # 前台运行（Ctrl+C 停）
scripts/notes/serve_site.sh --build        # 仅静态构建到 .site/
```

Windows 上用 `scripts/notes/serve_site.ps1`。

### 面向 coding agent

- [`AGENTS.md`](AGENTS.md) — 唯一权威入口
- [`CLAUDE.md`](CLAUDE.md) 只作索引指向 `AGENTS.md`，不维护独立副本

## 快速开始（构建）

### 依赖

- CMake 3.16+
- Ninja
- C++20 编译器
- Vulkan SDK
- SDL3（首版主线）或 GLFW

### Linux 构建

```bash
mkdir -p build && cd build
cmake .. -G Ninja
ninja demo_scene_viewer
```

### Linux 测试

```bash
ninja BuildTest
ctest --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --output-on-failure -L requires_video_device
```

- `Renderer` target 是 bootstrap / env-probe 可执行，不是主要交互入口
- 主 demo target 是 `demo_scene_viewer`
- 无桌面的 Linux 环境里，窗口类 Vulkan 测试通常需要 `xvfb-run`

## 常用工作流

命令入口位于 `.codex/commands/`（含 `opsx/` 子目录）。推荐链路：

```text
想法 / 问题
  -> /draft-req      可选，先把需求写清
  -> /opsx:propose   建立 OpenSpec change
  -> /opsx:apply     实施代码与测试
  -> /opsx:archive   归档 change 并同步 spec
  -> /finish-req     校验并归档需求文档
  -> /update-notes   同步 notes
  -> /refresh-notes  刷新本地 notes 站点
  -> git commit
```

常见短链路：

- 只讨论方案：`/opsx:explore`
- 只更新 notes：`/update-notes`
- 只同步设计索引：`/sync-design-docs`

## 目录约定

| 路径 | 用途 |
|---|---|
| `src/` | 代码主体（三层 + demos + 测试） |
| `assets/` | 资产（shaders / materials / models / textures / env） |
| `notes/` | 所有人类可读文档（唯一入口，原 `docs/` 与 `faq/` 已并入） |
| `openspec/specs/` | 当前实现契约（权威） |
| `openspec/changes/` | 活动中的变更提案 |
| `openspec/changes/archive/` | 已归档变更（历史记录，非当前事实源） |
| `scripts/notes/` | notes 站点工具（`serve_site.sh` / `generate_site_config.py` / `mkdocs_hooks.py`） |
| `scripts/source_analysis/` | 源码注释抽取工具 |
| `src/demos/scene_viewer/` | 主 demo |

## 开发原则

- 改子系统前，先读对应 `openspec/specs/<capability>/spec.md`
- 写 `notes/` 前，先读 `openspec/specs/notes-writing-style/spec.md`
- C++ 代码遵守 `openspec/specs/cpp-style-guide/spec.md`
- 文档只写当前可验证的事实，不保留旧目录 / 旧命令的兼容说明

## 下一步阅读

刚 clone 下来想搞清楚“这是什么”：

1. 跑一次 `scripts/notes/serve_site.sh`，浏览器打开本地站点
2. 读 [`notes/README.md`](notes/README.md) → [`notes/get-started.md`](notes/get-started.md) → [`notes/architecture.md`](notes/architecture.md)
3. 挑一个感兴趣的子系统，读对应 [`notes/subsystems/*.md`](notes/subsystems/) + 其 spec
4. 想看规划方向：[`notes/roadmaps/main-roadmap/README.md`](notes/roadmaps/main-roadmap/README.md)

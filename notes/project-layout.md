# 项目目录结构

比 `notes/README.md` 的速览更细，回答两个问题：

1. 仓库每个主要目录负责什么。
2. 哪些目录是当前事实来源，哪些是历史 / 产物 / 第三方依赖。

## 顶层结构

```text
renderer-demo/
├── src/
│   ├── core/
│   ├── infra/
│   ├── backend/
│   ├── demos/
│   └── test/
├── shaders/
├── assets/
├── notes/                 ← 所有人类可读文档的唯一入口
├── openspec/
├── scripts/
├── build/
├── CMakeLists.txt
├── mkdocs.yml
├── AGENTS.md
└── CLAUDE.md
```

## 哪些目录最重要

- `src/` 代码主体
- `openspec/specs/` 能力级规范，修改子系统前先看对应 spec
- `notes/` 所有人类可读文档的唯一根（需求 / 设计 / 子系统 / 教程 / 路线图 / 评审）
- `notes/subsystems/` 子系统设计文档入口
- `notes/requirements/` 活跃需求 + 历史归档（原 `docs/requirements/` 已并入）

## src/ 分层

```text
src/
├── core/              # 与具体图形后端解耦的接口、纯数据、算法
├── infra/             # 对 core 接口的通用基础设施实现
├── backend/vulkan/    # Vulkan 后端实现
├── demos/             # 正式交互 demo（demo_scene_viewer 等）
└── test/integration/  # 集成测试
```

### src/core/

`core` 不依赖 Vulkan 细节；跨后端稳定概念。

```text
src/core/
├── asset/         # mesh / texture / material / shader / skeleton / parameter_buffer
├── frame_graph/   # FrameGraph / FramePass / RenderQueue / RenderTarget
├── gpu/           # EngineLoop 等运行时编排层
├── input/         # IInputState / KeyCode / MouseButton / dummy / mock
├── math/          # 向量、矩阵、四元数、包围盒
├── pipeline/      # PipelineKey / PipelineBuildDesc
├── platform/      # 语义标量类型、窗口前向声明
├── rhi/           # Renderer 接口、IGpuResource、Buffer、VertexLayout、ImageFormat
├── scene/         # Scene / Object（SceneNode + IRenderable）/ Camera / Light / 相机控制器
├── time/          # Clock 等时间推进对象
└── utils/         # StringID / GlobalStringTable / filesystem helpers / env
```

### src/infra/

```text
src/infra/
├── gui/                 # GUI 抽象 + ImGui 实现 + debug_ui helper
├── material_loader/     # 通用 .material YAML loader（loadGenericMaterial）
├── mesh_loader/         # OBJ / GLTF 加载器
├── shader_compiler/     # shaderc 编译 + SPIRV-Cross 反射
├── texture_loader/      # 纹理加载 + placeholder（white / black / normal）
├── window/              # SDL3 / GLFW 窗口 + Sdl3InputState
└── external/            # vendored 第三方（SDL3 / imgui / stb 等）
```

### src/backend/vulkan/

```text
src/backend/vulkan/
├── vulkan_renderer.*         # Renderer 的 Vulkan 门面
└── details/
    ├── commands/             # command buffer 与 manager
    ├── descriptors/          # descriptor manager
    ├── device_resources/     # Vulkan Buffer / Texture
    ├── pipelines/            # Vulkan pipeline / pipeline_cache / graphics_shader_program
    ├── render_objects/       # framebuffer / render pass / swapchain
    ├── device.*              # VulkanDevice
    └── resource_manager.*    # 资源创建与缓存协调
```

### src/demos/

```text
src/demos/
└── scene_viewer/     # demo_scene_viewer：当前正式交互 demo 入口
```

### src/test/integration/

每个模块一个或几个集成测试可执行。覆盖 shader / pipeline / 材质 / scene 校验 / resource manager / frame graph 等。

## assets/

```text
assets/
├── materials/           # .material YAML 资产（blinnphong_* / pbr_gold 等）
├── shaders/glsl/        # GLSL 源（blinnphong_0 / pbr）
├── models/              # mesh 资产（DamagedHelmet 等）
├── textures/
└── env/
```

## notes/

所有人类可读文档的唯一根；`mkdocs.yml` 的 `docs_dir` 指向这里，`scripts/notes/serve_site.sh` 把它作为站点入口。

```text
notes/
├── README.md
├── get-started.md
├── project-layout.md         ← 本文
├── architecture.md
├── glossary.md
├── nav.yml
├── subsystems/               # 子系统设计（material / pipeline / scene / vulkan-backend / ...）
├── concepts/                 # 使用者视角概念文档（material / scene / camera / light / ...）
├── requirements/             # 活跃需求 + finished/ 归档（原 docs/requirements/ 并入）
├── design/                   # 早期设计草稿（原 docs/design/ 并入）
├── review/                   # 代码评审记录（原 docs/review/ 并入）
├── roadmaps/                 # 路线图（main-roadmap + research）
├── source_analysis/          # 文件级源码解析（由 @source_analysis.section 注释生成）
├── vulkan-backend/           # Vulkan 后端子页
├── tutorial/                 # 教程
├── ai-scanned/               # 历史 AI 扫描报告（只读快照）
├── tools/                    # notes 站点工具说明
├── temporary/                # 临时评审 / 讨论稿
└── assets/                   # 站点用图
```

子目录说明：

- `subsystems/` 面向维护者；`concepts/` 面向使用者
- `requirements/` 原 `docs/requirements/` 已合并到此目录；不再存在独立的 `docs/` 根
- `design/` / `review/` 从 `docs/design/` / `docs/review/` 合并进来
- `source_analysis/` 由 `scripts/source_analysis/extract_sections.py` 产出，不要手改
- `ai-scanned/` / `temporary/` 是历史快照 / 临时稿，不是当前事实来源

## openspec/

```text
openspec/
├── specs/               # 当前有效规范（权威）
└── changes/
    ├── archive/         # 已落地变更归档
    └── ...              # 活跃变更
```

`openspec/specs/` 是子系统行为的权威描述。`openspec/changes/archive/` 是历史记录，参考有用，不当作当前事实来源。

## scripts/

```text
scripts/
├── serve_site.sh / serve_site.ps1
├── generate_site_config.py
├── mkdocs_hooks.py
├── extract_sections.py
└── ...
```

- `serve_site.sh` 本地预览 `notes/` 站点
- `generate_site_config.py` 生成 `mkdocs.gen.yml` 并按 `notes/nav.yml` 组织导航
- `extract_sections.py` 从源码注释产出 `notes/source_analysis/`

## 哪些目录通常不需要改

- `build/`：构建产物
- `src/infra/external/`：第三方 vendored 代码
- `openspec/changes/archive/`：历史归档
- `notes/requirements/finished/`：历史需求
- `notes/ai-scanned/`：历史扫描快照

## 更新文档与代码一致性时的检查顺序

1. `src/`
2. `openspec/specs/`
3. `notes/subsystems/*.md`
4. `notes/concepts/*.md`
5. `notes/requirements/` 未 finished 的文档（当前只剩 `REQ-019`）
6. `notes/roadmaps/main-roadmap/`

## 推荐阅读顺序（新加入）

1. `notes/README.md`
2. 本文 `notes/project-layout.md`
3. `notes/architecture.md`
4. 目标子系统 `notes/subsystems/*.md`
5. 相关 `openspec/specs/<capability>/spec.md`

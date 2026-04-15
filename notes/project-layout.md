# 项目目录结构

这份文档比 `notes/README.md` 里的速览更细，目标是回答两个问题：

1. 当前仓库里每个主要目录负责什么。
2. 哪些目录是当前事实来源，哪些目录只是历史、产物或第三方依赖。

## 顶层结构

```text
renderer-demo/
├── src/
│   ├── core/
│   ├── infra/
│   ├── backend/
│   └── test/
├── shaders/
├── notes/
├── docs/
├── openspec/
├── scripts/
├── build/
├── third_party/
├── CMakeLists.txt
├── mkdocs.yml
├── AGENTS.md
└── CLAUDE.md
```

## 哪些目录最重要

- `src/` 是代码主体。
- `openspec/specs/` 是能力级规范，修改子系统前应该先看对应 spec。
- `notes/` 是给人读的导航与摘要，`scripts/serve-notes.sh` 会把它作为站点入口。
- `notes/subsystems/` 是当前子系统设计文档入口。
- `docs/requirements/` 是正在整理或计划中的需求文档。

## src/ 分层

```text
src/
├── core/              # 与具体图形后端解耦的接口、纯数据、算法
├── infra/             # 对 core 接口的通用基础设施实现
├── backend/vulkan/    # Vulkan 后端实现
└── test/integration/  # 集成测试
```

### src/core/

`core` 不应该依赖 Vulkan 细节。这里放的是跨后端稳定概念。

```text
src/core/
├── asset/         # 资产对象：mesh / texture / material / shader / skeleton
├── frame_graph/   # FrameGraph、FramePass、RenderQueue、RenderTarget
├── gpu/           # EngineLoop 等运行时编排层
├── math/          # 向量、矩阵、几何基础
├── pipeline/      # PipelineKey、PipelineBuildDesc
├── rhi/           # Renderer 接口、RenderResource、Buffer、ImageFormat
├── scene/         # Scene、Object、Camera、Light
├── time/          # Clock 等时间推进对象
└── utils/         # StringID、string table、日志、通用工具
```

说明：

- `asset/` 关注“是什么资源”。
- `rhi/` 关注“渲染硬件接口层需要什么抽象”。
- `pipeline/` 关注 pipeline 身份与构建描述。
- `frame_graph/` 关注场景初始化后如何组织 pass 和输出。
- `gpu/` 放 renderer 之上的运行时编排层，例如 `EngineLoop`。
- `time/` 放 `Clock` 这类和 frame 推进相关、但不属于 backend 的对象。
- `scene/` 保留场景层对象，不再混放 frame graph 组件。

### src/infra/

`infra` 是不直接属于 Vulkan backend、但又不是纯接口层的具体实现。

```text
src/infra/
├── gui/                 # GUI 抽象及 ImGui 实现
├── material_loader/     # 材质加载器
├── mesh_loader/         # OBJ / GLTF 网格加载器
├── shader_compiler/     # shaderc 编译 + SPIRV-Cross 反射
├── texture_loader/      # 纹理加载
├── window/              # SDL / GLFW 窗口实现
└── external/            # vendored 第三方代码
```

说明：

- `shader_compiler/compiled_shader.*` 是 `IShader` 的编译后实现。
- `mesh_loader/` 现在直接使用 `obj_mesh_loader.*`、`gltf_mesh_loader.*`，不再多包一层子目录。
- `external/` 是第三方源码，不应当按本项目命名规则大改。

### src/backend/vulkan/

这里是 Vulkan 专属实现，允许出现 Vulkan API 细节。

```text
src/backend/vulkan/
├── vulkan_renderer.*         # IRenderer 的 Vulkan 门面实现
└── details/
    ├── commands/             # command buffer 及其管理
    ├── descriptors/          # descriptor 管理
    ├── device_resources/     # Vulkan Buffer / Texture / Shader
    ├── pipelines/            # Vulkan pipeline 与 graphics shader program
    ├── render_objects/       # framebuffer / render pass / swapchain / context
    ├── device.*              # VulkanDevice
    └── resource_manager.*    # 资源创建与缓存协调
```

说明：

- `details/resources/` 已改成 `details/device_resources/`，避免和 `core` 的资源概念混淆。
- 旧的 `vk_` / `vkc_` / `vkd_` / `vkp_` / `vkr_` 文件名前缀已被更明确的全名替代。

### src/test/integration/

这里是按模块组织的集成测试，每个测试通常是一个独立可执行文件。

适合放在这里的测试：

- shader 编译与反射链路
- pipeline 描述收集逻辑
- 资源加载链路
- backend 的最小集成验证

## 其他重要目录

### shaders/

```text
shaders/
├── glsl/      # GLSL 源文件
└── spv/       # 构建后生成的 SPIR-V 或相关输出
```

### notes/

`notes/` 是给人读的项目站点源目录，`mkdocs.yml` 的 `docs_dir` 指向这里。

```text
notes/
├── README.md
├── architecture.md
├── glossary.md
├── subsystems/
├── tutorial/
└── roadmaps/
```

说明：

- `scripts/serve-notes.sh` 会先生成 `mkdocs.gen.yml`，再用 MkDocs 预览这个目录。
- `notes/subsystems/` 是当前系统设计说明的主入口。
- `notes/requirements/` 不是手写目录，而是由 `scripts/_gen_notes_site.py` 从 `docs/requirements/` 自动同步出来的链接目录。

### docs/

```text
docs/
├── design/          # 早期设计草稿/历史材料
└── requirements/    # 活跃需求文档
```

说明：

- `docs/design/` 仍可作为历史参考，但不再是当前设计事实来源。
- `docs/requirements/finished/` 是历史结果，不是当前活跃需求。

### openspec/

```text
openspec/
├── specs/               # 当前有效规范
└── changes/
    ├── archive/         # 已落地变更归档
    └── ...              # 活跃变更
```

说明：

- `openspec/specs/` 应被视为子系统行为的权威描述。
- `openspec/changes/archive/` 是历史记录，阅读有帮助，但不应当当作当前代码事实来源。

### scripts/

```text
scripts/
├── serve-notes.sh
├── _gen_notes_site.py
├── _notes_hooks.py
└── ...
```

说明：

- `serve-notes.sh` 负责启动 `notes/` 的本地站点。
- `_gen_notes_site.py` 会把活跃需求文档挂到导航里，并生成 `mkdocs.gen.yml`。

## 哪些目录通常不需要改

- `build/`：构建产物目录。
- `src/infra/external/`：第三方 vendored 代码。
- `openspec/changes/archive/`：历史归档。
- `docs/requirements/finished/`：历史需求。

如果要更新文档与代码的一致性，通常优先检查：

1. `src/`
2. `openspec/specs/`
3. `notes/`
4. `notes/subsystems/*.md`
5. `docs/requirements/` 中未 finished 的文档

## 推荐阅读顺序

新加入项目时，建议按这个顺序建立心智模型：

1. `notes/README.md`
2. 本文 `notes/project-layout.md`
3. `notes/architecture.md`
4. 目标子系统对应的 `notes/subsystems/*.md`

# 项目目录结构：哪些文件说明什么事实

项目目录像一座工厂的楼层图。我们先知道每层放什么，再决定读代码、改文档、查需求时从哪里进门。

## 顶层目录

```text
LXEngine/
├── src/                 # C++ 源码
├── assets/              # 运行时资产和测试资产
├── notes/               # 人类可读文档站点
├── openspec/            # 当前能力规范
├── scripts/             # notes、source analysis、editor 辅助脚本
├── third_party/         # 直接引入的第三方源码
├── build/               # 本地构建产物
├── CMakeLists.txt
├── AGENTS.md            # coding agent 的仓库入口
└── CLAUDE.md            # 指向 AGENTS.md 的兼容入口
```

## 代码分层先看 src/

| 目录 | 角色 | 典型内容 |
|---|---|---|
| `src/core/` | 平台无关核心 | scene、asset、material、frame graph、pipeline、RHI 接口、input、time |
| `src/infra/` | 工程工具层 | shader compiler/reflector、mesh/texture/material loader、window、ImGui |
| `src/backend/vulkan/` | Vulkan 后端 | device、swapchain、FrameGraph attachment、descriptor、pipeline、command buffer |
| `src/backend/vulkan/offline/` | Vulkan 离线后端 | headless compute renderer、GPU scene packing、BVH、readback |
| `src/demos/lxe_editor/` | 当前交互 editor | project、scene runtime、UI、API、recording、commands |
| `src/test/integration/` | 集成测试 | shader、material、scene、pipeline、resource、editor 相关测试 |
| `src/tools/` | 独立工具 | `lxe_offline_render` CLI、`lxe_manager` MCP 服务、`assets-downloader` React/TS 工具、Node 共享包 |

### `src/core/` 的当前子区

| 子目录 | 放什么 |
|---|---|
| `asset/` | `Mesh`、`Texture`、`MaterialTemplate`、`MaterialInstance`、`Shader`、`Skeleton` |
| `scene/` | `Scene`、`SceneNode`、components、camera、light、controller |
| `frame_graph/` | `FrameGraph`、`FramePass`、`RenderQueue`、`RenderTarget`、read/write resource 声明 |
| `offline/` | `OfflineRenderProfile`、`OfflineSceneIR`、离线 readback image |
| `pipeline/` | `PipelineKey`、`PipelineBuildDesc` |
| `rhi/` | renderer 接口、GPU resource、buffer、vertex layout |
| `gpu/` | `EngineLoop` |
| `input/` / `time/` / `math/` | 输入、时间、数学值类型 |
| `utils/` | string interning、filesystem helper、env helper |

### `src/infra/` 的当前子区

| 子目录 | 放什么 |
|---|---|
| `build_info/` | C++ 二进制 BuildInfo 字符串和 JSON，供 editor、offline renderer、输出 metadata 复用 |
| `material_loader/` | `.material` YAML loader |
| `mesh_loader/` | OBJ / glTF loader |
| `offline/` | scene 文档到 `OfflineSceneIR` 的 compiler、cache/project URI resolver |
| `scene_io/` | `.scene.yaml` 文档读写，供 editor 与 offline CLI 共用 |
| `shader_compiler/` | shaderc 编译与 SPIRV-Cross 反射 |
| `texture_loader/` | stb image 纹理加载与 placeholder textures |
| `window/` | SDL3 / GLFW window 和 SDL3 input |
| `gui/` | GUI 抽象与 ImGui 实现 |

## assets/ 是运行时文件入口

| 目录 | 当前用途 |
|---|---|
| `assets/materials/` | `.material` 文件 |
| `assets/shaders/glsl/` | GLSL 源和当前仓库内的 `.spv`，包括 forward、shadow、post、IBL bake、offline compute shader |
| `assets/models/` | 示例模型、测试模型、内置模型包 |
| `assets/models/builtin/` | 内置模型 manifest 和低面元资产 |
| `assets/textures/` | 独立贴图 |
| `assets/env/` | 环境贴图资产；资产存在不等于 HDR/Post 管线已经实现 |
| `assets/scenes/` | 仓库自带 scene |
| `assets/project_templates/` | 新建 project 的只读模板 |

## src/tools/ 是引擎外的实验工具层

| 工具 | 入口 | 当前用途 |
|---|---|---|
| `lxe_offline_render` | `src/tools/lxe_offline_render/` | 读取 `.scene.yaml` 与 `scene.offlineRender` profile，运行 Vulkan compute 离线渲染 MVP |
| `lxe_manager` | `src/tools/lxe_manager/` | 给 Codex 使用的 MCP 服务，管理 editor 进程、构建、日志和远端操作 |
| `assets-downloader` | `src/tools/assets-downloader/` | React + TypeScript 页面，管理外部资产下载、license、cache URI 和导入缓存 |
| `@lxe/build-info` | `src/tools/share/build-info/` | Node 工具共享 BuildInfo 字符串生成 |

这两个工具都服务于“场景资产可以被 editor 使用，也可以被离线 renderer 使用”的同一条路线。它们不应该反向依赖 `src/demos/lxe_editor/` 的 UI 状态；共享边界放在 `infra/scene_io`、`infra/offline`、`core/offline` 和 project/cache 路径约定上。

文件格式和 URI 细节见 [资产系统](../concepts/assets/index.md)。

## notes/ 现在按用途分区

| 目录 | 用途 | 阅读时机 |
|---|---|---|
| `notes/concepts-design/` | 本章入口、架构、目录、术语 | 第一次建立全局地图 |
| `notes/concepts/` | 资产、材质、引擎循环等概念 | 想理解一个系统为什么存在 |
| `notes/scene-system/` | scene、node、component、camera、light、renderable | 想理解场景运行时 |
| `notes/design/` | 跨模块设计说明，目前主要是 editor system | 想理解多个模块怎样协作 |
| `notes/tutorial/` | 动手路径 | 想按步骤操作 |
| `notes/source_analysis/` | 贴源码的实现解析 | 已有概念后深入源码 |
| `notes/requirements/` | 未实施或正在实施的需求 | 判断 future 能力状态 |
| `notes/roadmaps/` | 长期阶段规划和研究留档 | 判断方向和优先级 |
| `notes/tools/` | notes/editor manager 工具说明 | 维护文档站点和工具链 |

## 当前事实来源的优先级

当文档和代码看起来不一致时，我们按这个顺序判断：

1. `src/`
2. `openspec/specs/`
3. `notes/requirements/` 中 active 文档
4. `notes/concepts-design/`、`notes/concepts/`、`notes/scene-system/`、`notes/design/`
5. `notes/source_analysis/`
6. `notes/roadmaps/`
7. `notes/requirements/finished/`、`notes/ai-scanned/`、`notes/temporary/`

Roadmap 可以说明方向，但不能证明能力已经实现。是否实现要回到代码、spec 和 active/finished requirement。

## 继续阅读

- [架构总览](architecture.md)
- [术语表](glossary.md)
- [源码分析入口](../source_analysis/index.md)
- [Offline Renderer 教程](../tutorial/offline-renderer/index.md)

# GetStarted

我们先把 LXEngine 当成两间相连的工作室：`lxe_editor` 是实时搭景和调试工作台，`lxe_offline_render` 是无窗口的 offline ray tracer 实验台。第一次进入项目时，先把这两个入口跑通；材质、光源、shadow、PBR/IBL 和后续扩展都围绕它们展开。

## 最短路径

| 目标 | 命令 |
|---|---|
| 配置工程 | `cmake -S . -B build -G Ninja` |
| 验证 shader 编译 | `cmake --build build --target test_shader_compiler && ./build/src/test/test_shader_compiler` |
| 构建 editor | `cmake --build build --target lxe_editor -j2` |
| 启动 editor | `./build/src/editor/lxe_editor` |
| 构建 offline ray tracer | `cmake --build build --target CompileShaders lxe_offline_render test_render_work_compiler -j2` |

如果我们只想确认机器环境是否正常，先跑 `test_shader_compiler`。它不需要窗口，能最快暴露 `shaderc`、`glslc`、SPIRV-Cross 和 shader 文件路径问题。

## 先选入口：Editor 还是 Offline Ray Tracer

| 入口 | 当前用途 | 适合第一天做什么 |
|---|---|---|
| `lxe_editor` | 交互式实时 editor，负责打开 project、加载 scene、创建节点、调材质和保存场景 | 打开当前 Helmet/PBR scene，保存 scene |
| `lxe_offline_render` | headless offline renderer，读取同一份 scene 的 output profile / render path graph，输出 EXR/PNG/JSON/raw readback | 对 Helmet scene 跑 64x64 raytrace smoke |
| `test_shader_compiler` | shader 编译与反射 smoke | 检查本机 shader 工具链 |
| `test_vulkan_offline_renderer` | headless Vulkan offline renderer smoke | 排查 Vulkan device / offline backend 基础问题 |

新人教程默认从 `lxe_editor` 开始，因为 editor 能把场景、材质、光源、CommandBus、保存/加载和自动化状态放在同一个工作台里。Offline ray tracer 是第二个核心入口：它不依赖 swapchain，适合做可复现渲染输出、后续 path tracing、AOV 和质量参考实验。

## 跑通 Editor 主工作台

```bash
cmake -S . -B build -G Ninja
cmake --build build --target lxe_editor -j2
./build/src/editor/lxe_editor
```

启动后，我们优先验证三件事：

| 检查 | 说明 |
|---|---|
| editor 窗口能打开 | Vulkan、窗口系统和 demo 可执行程序正常 |
| scene 能直接打开 | `scene open assets/scenes/generated/helmet_standard_pbr.scene.yaml` 可排队加载 |
| scene 能保存和重新加载 | scene document、runtime scene 和 editor sidecar 状态闭合 |

后续材质、光源、shadow、PBR/IBL、自定义节点和 editor 扩展教程都会从这条工作台链路继续展开。

## 跑通 Offline Ray Tracer Smoke

Offline ray tracer 像一间独立实验室：我们仍然用 editor/scene YAML 搭场景，但渲染时不创建窗口和 swapchain，而是从命令行选择 output profile，让 profile 指向的 render path graph 通过 `FrameGraphExecutor` 执行，最后输出线性 float 图。

```bash
cmake --build build --target CompileShaders lxe_offline_render test_render_work_compiler -j2
ctest --test-dir build --output-on-failure -R 'test_render_work_compiler'
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile raytrace \
  --width 64 \
  --height 64 \
  --out artifacts/offline/smoke
```

成功后会生成：

```text
artifacts/offline/smoke.exr
artifacts/offline/smoke.png
artifacts/offline/smoke.json
artifacts/offline/smoke.rgba32f
```

`.exr` 是 scene-linear HDR 主输出，`.png` 是 tone-mapped preview，`.json` 记录 scene/profile/buildInfo 等复现信息，`.rgba32f` 是调试输出：每个像素 RGBA 四个 32-bit float。

当前 offline ray tracer 已经硬切到统一 FrameGraphExecutor 流程：它打通 scene 文件、`SceneResourceTable`、render path graph、`RenderInputDesc`、Vulkan graphics/compute pass、readback payload 和输出文件。多 bounce path tracing、高质量材质参考和更完整的 AOV 仍在后续 offline 教程中逐步展开。

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
| Offline Renderer | scene profile、offline ray tracer、headless FrameGraph、EXR/PNG 输出 | [Tutorial / Offline Renderer](tutorial/offline-renderer/index.md) |
| 自定义材质 | `.material`、contract shader、参数、Gooch 材质、editor 验证 | [Tutorial / 自定义材质](tutorial/custom-material/index.md) |
| 自定义灯光 | 三类内置 light、scene YAML、`SceneLightsUBO` 与当前 shader 边界 | [Tutorial / 自定义灯光](tutorial/custom-light/index.md) |
| Shadow 阶段 | Shadow pass、CSM、depth target 和多 pass 读写关系 | [Tutorial / Shadow 阶段](tutorial/shadow-era/index.md) |
| PBR + IBL | Damaged Helmet + neutral IBL 场景、资源与 shader 合同 | [Tutorial / PBR + IBL](tutorial/pbr-ibl/index.md) |
| 扩展编辑器 | toolbar 按钮、command、undo/API/MCP 复用 | [Tutorial / 扩展编辑器](tutorial/extend-editor/index.md) |
| 扩展场景节点 | 新 node kind、保存/加载、DebugDraw、兼容 editor 操作 | [Tutorial / 扩展场景节点](tutorial/extend-scene-node/index.md) |

## 相关工具不属于新手主线

| 工具 | 状态 | 说明 |
|---|---|---|
| [Assets Downloader](tools/assets-downloader.md) | 开发中（未完成） | 本地 Web 资产下载 / cache 工具，暂不作为教程主线或必需工作流 |
| [lxe_manager MCP 服务](tools/lxe-manager-mcp.md) | 可用 | agent / 自动化使用的 editor 管理与调试入口 |
| [Notes 工具链说明](tools/notes-tooling.md) | 可用 | notes 站点生成、预览和导航维护 |

Assets Downloader 保留在“相关工具”中，方便继续开发和验证；它暂时不作为 GetStarted、Tutorial 或 offline ray tracer 的必经步骤。

## 当前教程边界

GetStarted 和 Tutorial 只把当前代码可验证的路径放在主线里。读者可以按这些页面启动 editor、运行 offline ray tracer、保存 scene、调材质、看 directional shadow、验证 PBR/IBL，并阅读当前 command / toolbar / scene node 的手工扩展触点。

如果某个能力还没有落到当前代码中，它不应该在教程里伪装成“下一步工作流”。例如 light asset、custom light registry、toolbar registry、node kind registry 这类旧占位说法已经从教程主线清理掉；后续若重新设计，需要先回到 active requirement 或设计 spec，再进入教程。

## 继续阅读

- [Tutorial 总览](tutorial/index.md)
- [Offline Renderer](tutorial/offline-renderer/index.md)
- [场景系统](scene-system/index.md)
- [材质系统总览](concepts/material/index.md)

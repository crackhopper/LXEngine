# GetStarted

我们先把 LXEngine 当成一间正在搭建的教学工作室：底层已经有 Vulkan 渲染器、材质系统、场景对象和一个可交互的 `lxe_editor`；我们学习它时，不需要一开始就拆开所有机械结构，而是先学会开门、开灯、摆一个物体，再逐步理解背后的系统。

## 最短路径

| 目标 | 命令 |
|---|---|
| 配置工程 | `mkdir -p build && cd build && cmake .. -G Ninja` |
| 验证 shader 编译 | `ninja test_shader_compiler && ./src/test/test_shader_compiler` |
| 构建编辑器 | `ninja lxe_editor` |
| 启动编辑器 | `./src/demos/lxe_editor/lxe_editor` |
| 构建离线渲染器 | `ninja lxe_offline_render test_offline_scene_loader test_offline_gpu_scene` |

如果我们只想确认机器环境是否正常，先跑 `test_shader_compiler`。它不需要窗口和 GPU 交互，能最快暴露 `shaderc`、`glslc`、SPIRV-Cross 和 shader 文件路径问题。

## 我们现在主要使用哪个入口

当前新人教程默认以 `lxe_editor` 为主入口。`test_render_triangle` 仍然适合做底层 smoke test；主教学路径会从 editor 进入场景、材质、光源和扩展能力。

| 入口 | 适合做什么 |
|---|---|
| `test_shader_compiler` | 验证 shader 编译和反射链路 |
| `test_render_triangle` | 验证窗口、Vulkan backend、最小 draw loop |
| `lxe_editor` | 学习场景、材质、光源、编辑器命令和保存/加载 |
| `lxe_offline_render` | 读取 scene profile，运行 headless offline FrameGraph 离线渲染 MVP |

## 跑通 Offline Renderer MVP

离线渲染器像一间独立实验室：我们仍然用 editor/scene YAML 搭场景，但渲染时不创建窗口和 swapchain，而是从命令行把 scene 编译成 `SceneResourceTable`，再通过 offline `FrameGraph` 生成 compute work，最后输出线性 float 图。

从仓库根目录执行：

```bash
cmake -S . -B build -G Ninja
cmake --build build --target CompileShaders lxe_offline_render test_offline_image_writer test_offline_scene_loader test_offline_gpu_scene test_vulkan_offline_renderer -j2
ctest --test-dir build --output-on-failure -R 'test_offline_image_writer|test_offline_scene_loader|test_offline_gpu_scene|test_vulkan_offline_renderer|test_offline_render_cli'
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile mvp \
  --samples 1 \
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

## 启动 Assets Downloader

Assets Downloader 是本地资产管理工作台：我们用它下载外部 HDRI、模型、PBR 材质和 PLY 点云，把大文件整理进 `.asset_cache/`，场景文件只保存 `cache://` URI。

从仓库根目录执行：

```bash
corepack pnpm --dir src/tools/assets-downloader install
corepack pnpm --dir src/tools/assets-downloader dev
```

默认会启动：

| 服务 | 地址 |
|---|---|
| React UI | `http://127.0.0.1:5173/` |
| Fastify API | `http://127.0.0.1:4731/` |

3DGS train PLY 不进入 git。我们在 UI 中选择 `Voxel51 Gaussian Splatting` 数据源，使用 `Train point cloud, iteration 7000` 推荐项导入后，会得到：

```text
.asset_cache/voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/
  source.yaml
  raw/source.bin
  converted/point_cloud.ply
  converted/point_cloud.asset.yaml
```

后续 scene 应引用：

```text
cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/converted/point_cloud.ply
```

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
| Assets Downloader | 外部资源 catalog、license gate、`.asset_cache/` 和 `cache://` URI | [Tutorial / Assets Downloader](tutorial/assets-downloader/index.md) |
| Offline Renderer | scene profile、headless offline FrameGraph、path tracing 扩展点 | [Tutorial / Offline Renderer](tutorial/offline-renderer/index.md) |
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
- [场景系统](scene-system/index.md)
- [材质系统总览](concepts/material/index.md)

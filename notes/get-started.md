# GetStarted

这份文档给第一次进入 `LXEngine` 的人一个最短可执行路径：先知道项目现在是什么，再用一组真实命令把它构建起来，最后跑通一个最小示例。

如果你只想先确认工程能不能工作，建议按这个顺序：

1. 先跑 `test_shader_compiler`，验证 shader 编译与反射链路，不需要 GPU。
2. 再跑 `test_render_triangle`，验证窗口、Vulkan backend 和渲染主循环。
3. 跑通后再回头看架构和子系统文档，不要一开始就陷进细节。

## 先建立正确预期

当前仓库虽然顶层 target 名叫 `Renderer`，但 `src/main.cpp` 现在只是一个 bootstrap / env-probe 入口。对新人来说，真正更有价值的入口不是它，而是：

- `src/test/integration/test_shader_compiler.cpp`
- `src/test/test_render_triangle.cpp`
- `src/demos/lxe_editor/main.cpp`

前者验证“shader 源码读取 -> `shaderc` 编译 -> SPIR-V 反射”这条链路；`test_render_triangle` 验证“窗口 -> renderer -> scene -> engine loop -> draw”这条最小可运行路径；`lxe_editor` 则是当前正式的交互编辑器入口。

## 环境准备

### 必需依赖

在 Linux 上，至少要准备这些基础能力：

- C++20 编译器
- `cmake` 3.16+
- `ninja`
- Vulkan SDK 或系统 Vulkan 开发环境
- `glslc`
- `shaderc`

项目对依赖的处理方式有两个层次：

- `Vulkan` 是必需依赖，`find_package(Vulkan REQUIRED)` 找不到就会直接失败。
- `shaderc` 也是必需依赖，找不到会直接报错。
- `SPIRV-Cross` 如果本机没有，会自动走 `FetchContent` 从源码拉取。
- `SDL3` 在 Linux 下如果本机没有，也会自动走 `FetchContent`。

也就是说，最容易卡住的新手问题通常不是 `SDL3`，而是：

- 没装 Vulkan 开发环境
- 没装 `glslc`
- 没装 `shaderc`

### Linux 上的最低检查

先确认这些命令存在：

```bash
cmake --version
ninja --version
glslc --version
```

如果 `glslc` 不存在，shader 的预编译 target `CompileShaders` 会失败。

如果你已经安装了 Vulkan SDK，但 CMake 仍然找不到依赖，可以检查：

```bash
echo "$VULKAN_SDK"
```

这个工程会尝试从 `VULKAN_SDK` 里找一部分库和工具。

## 构建项目

下面这组命令是当前仓库在 Linux 上最直接的构建路径：

```bash
mkdir -p build
cd build
cmake .. -G Ninja
```

默认窗口后端是 SDL：

```cmake
option(USE_SDL "Use SDL2 as the window backend" ON)
option(USE_GLFW "Use GLFW as the window backend" OFF)
```

所以如果你没有特殊需求，不需要额外传参。

如果你想显式写出来，也可以这样：

```bash
cmake .. -G Ninja -DUSE_SDL=ON -DUSE_GLFW=OFF
```

### 构建时你会看到什么

- `src/` 下的 `core / infra / backend / test` 会被分别加入构建。
- `assets/shaders/CMakeLists.txt` 会创建 `CompileShaders` target，把 `assets/shaders/glsl/*.vert` 和 `*.frag` 编译成 SPIR-V。
- 顶层 `Renderer` target 会被生成，但它当前只是 bootstrap / env-probe；真正的交互入口是 `lxe_editor`。

## 第一步：先跑无 GPU 的验证

先只验证 shader 编译与反射链路：

```bash
ninja test_shader_compiler
./src/test/test_shader_compiler
```

为什么先跑它：

- 它不依赖窗口系统和 GPU。
- 它能最快暴露 `shaderc`、`SPIRV-Cross`、shader 文件读取是否正常。
- 如果这里都没过，就没必要马上排查 Vulkan 渲染问题。

如果这个步骤失败，优先检查：

- `shaderc` 是否被 CMake 找到
- `glslc` 是否存在
- Vulkan SDK / 头文件 / 库是否可见

## 第二步：跑第一个实际渲染示例

最小渲染示例是 `src/test/test_render_triangle.cpp`。它做的事情很明确：

- 初始化窗口
- 创建 `VulkanRenderer`
- 组装一个最小 mesh / material / skeleton / scene
- 通过 `EngineLoop` 启动场景
- 在 update hook 里更新 camera
- 进入主循环持续绘制

构建并运行：

```bash
ninja test_render_triangle
./src/test/test_render_triangle
```

这个 target 依赖 `CompileShaders`，所以 shader 的 SPIR-V 会先被生成。

### 运行成功时应该看到什么

- 弹出一个窗口
- 进入持续渲染循环
- 可以通过关闭窗口结束程序

如果你只想确认它是否真的进入了 draw 路径，可以打开调试输出：

```bash
LX_RENDER_DEBUG=1 ./src/test/test_render_triangle
```

这个环境变量会让测试程序额外打印窗口尺寸、相机参数、顶点变换等调试信息。

## 常见问题

### `shaderc not found`

这是 `src/infra/CMakeLists.txt` 的硬错误。说明 CMake 没找到 `shaderc`。在 Linux 上先补齐系统开发包，或者手动传：

```bash
cmake .. -G Ninja -DSHADERC_DIR=/path/to/shaderc
```

### `SPIRV-Cross` 找不到

这通常不是阻塞问题。项目会自动走 `FetchContent`。只有在网络受限或拉取失败时，才需要手动指定：

```bash
cmake .. -G Ninja -DSPIRV_CROSS_DIR=/path/to/spirv-cross
```

### Vulkan 相关错误

如果 `find_package(Vulkan REQUIRED)` 失败，先修复本机 Vulkan 开发环境，而不是修改工程。

### 程序启动后没有窗口或立即退出

优先检查：

- 当前机器是否有可用 Vulkan 驱动
- 是否在图形环境下运行，而不是纯 headless shell
- `test_render_triangle` 的运行目录是否正常找到 shader 资源

当前主路径已经优先走“显式 runtime root”约定，而不是启动时猜 cwd。默认开发态 runtime root 需要能看到：

- `assets/`
- `assets/materials/`
- `assets/shaders/glsl/`

仓库内通常是 repo root；`build/` 目录也会通过 CMake 创建自己的 `assets/` 树：非 shader 资产会被同步进去，shader 源码和 `.spv` 会落在 `build/assets/shaders/glsl/`，方便直接从 build root 跑测试。

## 建议的阅读顺序

跑通上面的两步之后，再按下面顺序建立心智模型会更省时间：

1. [项目速览](README.md)
2. [架构总览](architecture.md)
3. [项目目录结构](project-layout.md)
4. [概念 / 引擎循环](concepts/engine-loop.md)
5. [设计 / 子系统总览](subsystems/index.md)
6. [设计 / ShaderSystem](subsystems/shader-system.md)
7. [设计 / Scene](subsystems/scene.md)
8. [设计 / VulkanBackend](subsystems/vulkan-backend.md)

如果你的目标不是读源码，而是尽快做出一个自己的例子，下一站更应该看：

- [Tutorial](tutorial/00-overview.md)

## RTR 第五章实验入口

如果目标是学习并验证 Real-Time Rendering 第五章里的光源和 Gooch shading，这个仓库里最适合的入口是 `lxe_editor`，而不是 `test_render_triangle`。前者能承载“创建测试场景 -> 切换材质 -> 保存场景 -> 重新加载验证”的工作流。

当前已经存在的底层能力：

| 能力 | 当前状态 |
|---|---|
| Shader / material 资产 | `.material` 能声明 shader、passes、parameters、resources，并由 `GenericMaterialLoader` 通过反射校验 |
| Shader 编译 | `assets/shaders/glsl/*.vert` / `*.frag` 会进入 `CompileShaders` |
| 场景文件 | `lxe_editor` 已有 `.scene.yaml` 的 load / save 路径 |
| 光源 runtime | core 层已有 `LightBase` 与 `DirectionalLight`，scene 可以持有 light 列表 |
| 实验编辑器 | `lxe_editor` 已有 Scene Tree、Inspector、CommandBus、Undo / Redo 基础 |

目前 active requirements 正在补齐实验所需的作者底座：

| REQ | 作用 |
|---|---|
| [REQ-041-d](requirements/041-d-scene-authoring-toolbar-palette.md) | 从工具栏快速拖拽创建 primitive、camera、directional light |
| [REQ-041-e](requirements/041-e-scene-authoring-inspector-material-and-visibility.md) | 在 Inspector 里切换材质、编辑节点级颜色覆盖、简化 visibility |
| [REQ-041-f](requirements/041-f-scene-authoring-node-rename-duplicate.md) | 让测试节点能 rename、copy、paste，并保持 scene file 语义 |
| [REQ-041-g](requirements/041-g-rtr-light-experiment-foundation.md) | 补齐 Directional / Point / Spot Light 的可保存、可编辑、可绑定底座 |
| [REQ-041-h](requirements/041-h-rtr-material-experiment-foundation.md) | 补齐实验材质接入、材质候选列表、节点级通用参数覆盖 |

在这些需求落地前，新增实验材质的最小手动路径是：

```bash
# 1. 新增或复制一个材质 YAML
cp assets/materials/blinnphong_lit.material assets/materials/rtr_experiment_local.material

# 2. 新增对应 shader 源，命名要和 material 里的 shader 字段一致
#    例如 assets/shaders/glsl/rtr_experiment_local.vert
#    以及 assets/shaders/glsl/rtr_experiment_local.frag

# 3. 重新构建 shader 编译验证
cd build
ninja test_shader_compiler
./src/test/test_shader_compiler

# 4. 运行交互场景查看效果
ninja lxe_editor
./src/demos/lxe_editor/lxe_editor
```

`041-d` 到 `041-h` 完成后，推荐实验循环会变成：

1. 在 `lxe_editor` 里用工具栏拖入一个 primitive。
2. 从 Inspector 把节点切到 `assets/materials/rtr_*.material` 实验材质。
3. 拖入 Directional / Point / Spot Light，调整光源参数。
4. 在 Inspector 修改节点级材质参数覆盖；默认只影响当前节点。
5. 保存 `.scene.yaml`，重新加载同一场景验证参数和光源是否 round-trip。
6. 迭代 shader / material 文件，重新构建后用同一场景验证视觉结果。

这里的关键边界是：需求只负责让实验环境能够快速搭建和保存；Gooch shading 公式、多光源着色公式、衰减模型等学习内容不由这些底座需求替我们实现。

## 一句话版本

把这个项目跑起来的最短路径不是研究 `Renderer` 可执行文件，而是：

先 `test_shader_compiler`，再 `test_render_triangle`，跑通后再读 architecture 和 subsystems。

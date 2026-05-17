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

当前新人教程默认以 `lxe_editor` 为主入口。`test_render_triangle` 仍然适合做底层 smoke test；主教学路径会从 editor 进入场景、材质、光源和扩展能力。

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
- [场景系统](scene-system/index.md)
- [材质系统总览](concepts/material/index.md)

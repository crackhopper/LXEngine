# 01 环境与构建

构建环境像工作室的电源、水管和工具箱。我们还没有开始创作场景，先确认 CMake 能生成工程，Ninja 能编译目标，shader 工具能把 GLSL 翻译成 Vulkan 需要的 SPIR-V。

## CMake 负责生成工程图

CMake 不是编译器。它更像“工程图生成器”：读取仓库里的 `CMakeLists.txt`，识别源文件、库、可执行程序、依赖关系和编译选项，然后为本机工具生成一套 build 文件。Linux 上我们通常让它生成 Ninja 工程，后续真正执行编译的是 `ninja`。

| 工具 | 负责什么 | 类比 |
|---|---|---|
| `cmake` | 读取 `CMakeLists.txt`，生成 build 系统 | 画施工图 |
| `ninja` | 按 build 系统编译指定 target | 按图施工 |
| `glslc` | 把 `assets/shaders/glsl/*.vert|*.frag` 编译成 `.spv` | 把剧本翻译成 GPU 能读的语言 |
| `ctest` | 按 CMake 注册的测试列表运行测试 | 按验收清单逐项检查 |

Vulkan 是渲染后端的地基。CMake 配置阶段会在 `src/backend/CMakeLists.txt` 中查找 Vulkan；地基不在，窗口、渲染测试和 editor 都无法正常工作。

## LXEngine 的 CMake 入口如何分工

LXEngine 没有 `src/CMakeLists.txt`。顶层 `CMakeLists.txt` 直接把几个子目录加入工程，各个子目录再用自己的 `CMakeLists.txt` 定义库、测试和可执行程序。

| 文件 | 当前职责 |
|---|---|
| `CMakeLists.txt` | 顶层入口；设置 C++20、测试开关、全局选项，并加入 `src/core`、`src/infra`、`src/backend`、`src/test`、`src/demos`、`assets/shaders` |
| `src/core/CMakeLists.txt` | 定义 `LX_Core`，收集 core 源文件和核心 include 路径 |
| `src/infra/CMakeLists.txt` | 定义 `LX_Infra`，处理 SDL/GLFW、shaderc、SPIRV-Cross、yaml-cpp、ImGui 等基础设施依赖 |
| `src/backend/CMakeLists.txt` | 定义 `LX_Backend`，查找 Vulkan 并连接 core / infra |
| `src/test/CMakeLists.txt` | 定义集成测试 target、`BuildTest` 聚合 target，并把测试注册到 CTest |
| `src/demos/lxe_editor/CMakeLists.txt` | 定义 `lxe_editor`，连接 editor 所需库，并依赖 `CompileShaders` |
| `assets/shaders/CMakeLists.txt` | 定义 `CompileShaders`，调用 `glslc` 生成 build 目录下的 `.spv` |

这样组织的好处是职责清楚：顶层只安排工程结构，具体模块在自己的目录里说明如何构建。

## 先配置 build 目录

我们从仓库根目录开始。第一条命令只创建 build 目录，不会编译任何 C++ 文件：

```bash
mkdir -p build
cd build
```

接着让 CMake 读取上一层源码目录，并生成 Ninja 工程：

```bash
cmake .. -G Ninja
```

这条命令里的参数分别表示：

| 片段 | 含义 |
|---|---|
| `cmake` | 启动 CMake 配置阶段 |
| `..` | 源码目录在 build 目录的上一层，也就是仓库根目录 |
| `-G Ninja` | 让 CMake 生成 Ninja build 文件 |

配置阶段会检查编译器、Vulkan、shaderc、SPIRV-Cross、yaml-cpp 等依赖。只要这里失败，后面的 `ninja` 还没有机会开始编译。

## 再编译一个无窗口验证目标

第一次不要急着打开 editor。我们先编译并运行 `test_shader_compiler`，因为它不需要窗口，却能验证 shader 编译与反射链路：

```bash
ninja test_shader_compiler
./src/test/test_shader_compiler
```

两条命令做的事情不同：

| 命令 | 作用 |
|---|---|
| `ninja test_shader_compiler` | 编译这个测试 target 以及它依赖的 core / infra / backend 代码 |
| `./src/test/test_shader_compiler` | 运行生成出来的测试程序 |

这一步能先帮我们确认 `shaderc`、SPIRV-Cross、shader 文件路径和反射链路没有问题。窗口相关问题留到下一篇启动 editor 时再处理。

## 常用构建选项

有些选项在配置阶段传给 CMake，格式是 `-DNAME=value`。如果 build 目录已经配置过，改选项后重新运行 `cmake .. -G Ninja -D...` 即可。

| 选项 | 默认值 | 作用 |
|---|---|---|
| `USE_SDL` | `ON` | 使用 SDL 窗口后端 |
| `USE_GLFW` | `OFF` | 使用 GLFW 窗口后端 |
| `LX_BUILD_DEMOS` | `ON` | 是否加入 `src/demos`，其中包含 `lxe_editor` |
| `SHADERC_DIR` | 空 | 指向自定义 shaderc 安装目录 |
| `SPIRV_CROSS_DIR` | 空 | 指向自定义 SPIRV-Cross 安装目录 |
| `LX_ENABLE_SANITIZERS` | `OFF` | 在 GCC/Clang 下启用 ASan + UBSan |

例如我们只想配置 SDL 后端并显式保留 demos：

```bash
cmake .. -G Ninja -DUSE_SDL=ON -DUSE_GLFW=OFF -DLX_BUILD_DEMOS=ON
```

## CMake 配置失败时先看这些点

| 现象 | 优先检查 |
|---|---|
| `shaderc not found` | 系统是否安装 shaderc 开发包，或是否传了 `SHADERC_DIR` |
| `glslc: command not found` | Vulkan SDK 或系统 shader tools 是否安装 |
| `Vulkan REQUIRED` 失败 | Vulkan SDK / 驱动 / CMake package 是否可见 |
| CMake 成功但 shader 失败 | `assets/shaders/glsl/` 下 shader 是否存在 |

## 我们已经学会了什么

我们已经把构建链路串起来了：`CMakeLists.txt` 描述工程，`cmake` 生成 Ninja 工程，`ninja` 编译目标，`glslc` 通过 `CompileShaders` 生成 SPIR-V。这样后面 editor 起不来时，我们能先区分是配置问题、编译问题、shader 问题，还是窗口/Vulkan 运行时问题。

## 下一步

进入 [02 启动 editor](02-start-editor.md)。

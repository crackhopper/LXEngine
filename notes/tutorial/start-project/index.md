# 启动项目：从能构建到能保存场景

我们把第一次运行 LXEngine 想成走进一间工作室：先确认电源和工具都在，再打开 `lxe_editor`，最后摆放一个简单场景并保存。这个系列只解决一件事：让新人能独立把项目跑起来。

## 第一次运行要跨过哪几道门槛

| 门槛 | 为什么先学它 | 对应章节 |
|---|---|---|
| 工具链 | CMake、Ninja、Vulkan、shader 工具先要可用 | [01 环境与构建](01-environment-and-build.md) |
| editor 可执行程序 | 后续教程都在 `lxe_editor` 里验证 | [02 启动 editor](02-start-editor.md) |
| project / scene 读写 | 先从模板创建项目，再在项目里保存多个 scene | [03 加载与保存场景](03-load-and-save-scene.md) |
| 最小编辑循环 | 创建、选择、修改、预览、保存是所有作者工作的基础 | [04 基础场景编辑](04-basic-authoring.md) |
| 失败分流 | 出错时先判断失败发生在哪一层 | [05 启动排错](05-troubleshooting.md) |

## editor、项目目录和本地数据的关系

`lxe_editor` 像一张工作台：project 是当前工作室，scene 是桌上的布置图，资产目录是柜子里的材料，CommandBus 是每个动作背后的操作记录。我们用按钮摆物体，但按钮背后仍然会发出命令；这样人、测试和 agent 都能走同一条路。

| 部分 | 位置 | 在启动流程中的作用 |
|---|---|---|
| editor 入口 | `src/demos/lxe_editor/main.cpp` | 编译出 `lxe_editor` 可执行程序 |
| editor 会话 | `src/demos/lxe_editor/editor_session.*` | 组织 project、scene runtime、UI 和 command |
| 项目模板 | `assets/project_templates/` | 提供只读项目起点 |
| 本地项目 | `data/projects/` | 保存练习 project、scenes 和项目资产 |
| 本地配置 | `data/lxe_editor/` | 保存 editor 布局、command history、API 发现信息 |
| 命令总线 | `src/core/editor/command_bus.*` | 让 UI、console、API 和 MCP 复用同一套操作 |

## 完成本系列后我们能做什么

完成本系列后，我们应该能从空 build 目录配置工程，启动 editor，从 `empty` template 创建自己的 project，在 project 里新建、打开和保存 scene，并在常见失败发生时知道先检查哪一层。

## 下一步

进入 [01 环境与构建](01-environment-and-build.md)。

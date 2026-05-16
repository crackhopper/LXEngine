# CommandBus 心智模型：一条线路接多个开关

`CommandBus` 像房间里的电路：墙上开关、遥控器、自动化脚本都不直接碰灯泡，而是把“开灯”这个意图送进同一条线路。LXEngine 的 editor 也采用这个原则，toolbar、Inspector、console、HTTP API 和 MCP 诊断入口最终都应复用 command surface。

## 为什么 command-first 重要

| 如果直接改状态 | 如果 dispatch command |
|---|---|
| 每个入口都要重复实现规则 | 规则集中在 command handler |
| undo/redo 容易漏掉 | command 可以统一记录历史 |
| 自动化测试难复用 UI 行为 | 测试可以直接发命令 |
| API/MCP 与 UI 行为可能分叉 | 所有入口共享同一结果 |

这个结构让 editor 更容易被人使用，也更容易被脚本和测试驱动。

## CommandBus 在仓库里的连接点

| 文件 | 作用 |
|---|---|
| `src/core/editor/command_bus.hpp` | command 注册、dispatch、history、completion 的核心接口 |
| `src/core/editor/commands/builtin_commands.cpp` | 内置 command handler 与补全 |
| `src/core/editor/console_input_controller.cpp` | console 输入如何 dispatch command |
| `src/demos/lxe_editor/ui_overlay.*` | 浮动 toolbar 与 editor UI |
| `src/demos/lxe_editor/lxe_editor_api_service.cpp` | HTTP/WebSocket/MCP 如何复用 command |

我们读这些文件时，可以把问题固定成一句话：这个入口最后是否变成了一条 command line。

## 一个按钮的理想路径

| 阶段 | 对象 | 类比 |
|---|---|---|
| 用户点击 | toolbar action | 按下遥控器按钮 |
| 构造命令 | command line | 遥控器发出信号 |
| 分发命令 | `CommandBus::dispatch` | 信号进入线路 |
| 执行业务 | command handler | 继电器改变电路 |
| 记录结果 | command history | 留下操作记录 |

当我们新增 toolbar 按钮时，最好先问：它应该 dispatch 哪个 command。如果答案不清楚，通常应该先设计 command，再设计按钮。

## 我们已经学会了什么

我们建立了 editor 扩展的基本原则：按钮不是业务逻辑的家，command handler 才是共享行为的家。

## 下一步

进入 [02 读懂当前内置命令](02-read-current-commands.md)，看现有 command 是怎样注册和补全的。

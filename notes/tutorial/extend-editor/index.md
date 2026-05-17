# 扩展编辑器：按钮是遥控器，命令才是线路

编辑器扩展可以先理解成一套遥控系统：toolbar 按钮是手里常按的遥控器，`CommandBus` 是墙里的线路，Inspector、HTTP API、MCP 诊断通道也都接到同一条线路上。这个系列会先讲当前 command-first 结构，再讲未来更适合扩展的 command / toolbar 注册模型。

## 一个按钮背后为什么先要有 command

如果按钮直接改 editor state，Console、HTTP API、MCP 和测试就很难复用同一行为。Command-first 的价值，是让所有入口先表达同一条命令，再由 handler 集中执行规则。这个系列先读懂这条线路，再讨论 toolbar action metadata 为什么能减少重复接线。

## 当前手工路径暴露出的重复劳动

| 触点 | 当前为什么要同步 |
|---|---|
| command handler | 真正执行行为 |
| completion / help | 让 Console 能发现命令 |
| toolbar UI | 让常用命令可点击 |
| API / MCP snapshot | 让远程诊断能观察状态 |
| tests / recordings | 确认 UI 与 command 没有分叉 |

## 当前可实践章节

| 章节 | 我们学什么 |
|---|---|
| [01 CommandBus 心智模型](01-command-bus-model.md) | 为什么按钮应该 dispatch command |
| [02 读懂当前内置命令](02-read-current-commands.md) | command handler、补全、history 在哪里 |
| [03 手工增加一个命令](03-add-command-current-path.md) | 当前 C++ 改造路径 |
| [04 手工增加一个 toolbar 按钮](04-add-toolbar-button-current-path.md) | 当前 UI 接入路径 |
| [05 未来扩展注册表](05-future-extension-registry.md) | metadata 如何统一 command、toolbar、API |

## 未来 registry 章节为什么放在最后

| 路径 | 状态 | 说明 |
|---|---|---|
| 使用现有 command | 当前可用 | command console、toolbar、API、MCP 复用 `CommandBus` |
| 新增内置 command / toolbar 按钮 | 当前可做 | 需要改 C++ handler、UI、补全和测试 |
| extension registry | 未来能力 | 由 [REQ-042-b](../../requirements/pending/042-b-tutorial-editor-extension-registry.md) 跟踪 |

未来 metadata 只有在我们理解 command-first 的当前价值后才有意义：它不是绕过 C++ handler，而是让命令说明、按钮说明、补全和 API schema 读同一份信息。

## 完成后我们能判断什么

这个系列会把 editor 扩展拆成两个角色：命令表达“做什么”，toolbar 表达“在哪里触发”。

## 下一步

进入 [01 CommandBus 心智模型](01-command-bus-model.md)。

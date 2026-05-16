# 读懂当前内置命令：先看线路图

在扩展 editor 前，我们先读线路图。当前内置命令集中在 `builtin_commands.cpp`，它既定义命令怎样执行，也定义很多补全入口。对新人来说，先读已有命令比直接新增命令更稳。

## CommandBus 的核心对象

| 对象 | 作用 | 我们关注什么 |
|---|---|---|
| `CommandBus` | 注册与分发命令 | command 是否存在、如何 dispatch |
| `RegisteredCommand` | 一条命令的 handler | handler 接收哪些参数 |
| `CommandResult` | 执行结果 | 成功、失败、提示信息 |
| `HistoryEntry` | 历史记录 | undo/redo 与诊断信息 |
| completion | 输入补全 | command console 和自动化体验 |

这些对象像一张线路图里的符号。新增命令前，先确认它会怎样出现在这张图上。

## 阅读顺序

| 顺序 | 文件或函数 | 目标 |
|---|---|---|
| 1 | `command_bus.hpp` | 看 `dispatch`、`complete`、history 的接口 |
| 2 | `builtin_commands.cpp` | 找一个简单命令作为样板 |
| 3 | `registerBuiltinCommands` | 看命令集中在哪里注册 |
| 4 | completion 相关代码 | 看输入提示如何返回 |
| 5 | 调用方 | 看 toolbar、panel、API 如何 dispatch |

不要一开始就跳到 UI。UI 只是入口，command 才是行为。

## 一个小练习

我们选择 `state toolbar` 这类只读命令作为阅读样板。它适合入门，因为不会改变场景，也更容易通过 command console 或 API 观察结果。

| 观察点 | 读代码时的问题 |
|---|---|
| 命令名 | 顶层 verb 是什么 |
| 参数 | 是否有子命令或选项 |
| 结果 | 成功时返回什么文本或数据 |
| 错误 | 参数不合法时怎样提示 |
| 补全 | 输入一半时是否出现建议 |

## 我们已经学会了什么

我们知道读 command 的顺序：先看 bus 接口，再看注册点，再看 handler，最后看 UI 和 API 怎样复用它。

## 下一步

进入 [03 手工增加一个命令](03-add-command-current-path.md)，用当前 C++ 路径设计一条小命令。

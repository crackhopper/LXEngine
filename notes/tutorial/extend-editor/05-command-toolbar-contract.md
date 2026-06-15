# Command / Toolbar 合同：按钮只是命令入口

当前 editor 扩展没有独立的 metadata registry。我们真正能依赖的是 command-first 合同：所有入口都 dispatch 同一条命令，状态变化集中在 handler，toolbar、Console、HTTP API、MCP 和测试观察同一个结果。

## 当前合同

| 角色 | 应该承担什么 |
|---|---|
| Command handler | 解析参数、执行行为、返回 `CommandResult`、维护 undo/redo 元数据 |
| Completion / help | 让 Console 能发现命令和参数 |
| Toolbar button | 根据当前 selection / mode 决定 enabled 状态，然后 dispatch command |
| API / MCP snapshot | 暴露 editor 当前状态，方便自动化和远程诊断 |
| Tests / recordings | 验证 command 行为和 UI 入口没有分叉 |

新增按钮时，先确认是否已经有对应 command。没有 command 时先补 command；按钮不应该直接改 scene 或 editor state。

## 一个新增入口的检查表

| 检查 | 判断标准 |
|---|---|
| Console 能执行 | 同一条命令在 console 中可用，失败信息可读 |
| Toolbar 能触发 | 按钮只负责构造命令行并 dispatch |
| 禁用状态正确 | 无 selection、错误 mode、缺资源时按钮不可点或返回明确错误 |
| API/MCP 可观察 | 远程 snapshot 能看到行为所依赖的关键状态 |
| undo/redo 明确 | 改 scene 状态的命令要有历史语义或明确说明不可撤销 |
| 测试覆盖 | 至少覆盖成功路径和一个稳定失败路径 |

## 为什么现在不讲 registry

旧教程曾把 command / toolbar registry 当成 future path 链接到过期需求，这会误导读者以为下一步是找某个 registry API。当前代码事实不是这样。当前可靠路径仍然是读 `CommandBus`、`registerBuiltinCommands`、toolbar dispatch 和 API snapshot，把这些点手工对齐。

如果以后重新引入 extension metadata，也应该是对这份合同的归纳，而不是替代 handler 或绕开 command bus。

## 我们已经学会了什么

我们知道 editor 扩展的核心不是“在哪里画按钮”，而是“行为是否能被所有入口复用、观察和测试”。

## 下一步

继续进入 [扩展场景节点](../extend-scene-node/index.md)，学习新节点如何兼容选择、保存、调试绘制和 command 操作。

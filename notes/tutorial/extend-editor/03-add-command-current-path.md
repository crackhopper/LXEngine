# 手工增加一个命令：先铺线路，再接按钮

当前新增 command 像在墙里加一条新线路：我们要定义命令名、参数、handler、返回结果和补全，再写测试确认它能从不同入口触发。这个路径当前可行，但仍然偏手工。

## 设计一个示例命令

我们用 `scene mark-debug <node>` 做教学例子：它的目标是给一个 scene node 加上调试标记。这个命令在当前仓库里不是现成能力，教程把它作为扩展样板。

| 项 | 设计 |
|---|---|
| verb | `scene` |
| 子命令 | `mark-debug` |
| 参数 | node path |
| 行为 | 给目标节点设置 debug 标记 |
| 返回 | 成功说明或错误原因 |
| undo | 如果改变 scene 状态，应进入 undo/redo 设计 |

## 当前手工修改点

| 修改点 | 目的 |
|---|---|
| `builtin_commands.cpp` | 注册 handler，解析参数，返回 `CommandResult` |
| completion 逻辑 | 输入 `scene mark-...` 时给出提示 |
| scene / editor state | 存放 debug 标记状态 |
| UI 调用方 | 如果有按钮，再 dispatch 这条命令 |
| 测试 | 覆盖成功、未知节点、参数缺失、history |

这一步的关键是把状态变化放进 command handler，而不是让 toolbar 按钮直接改 scene。

## Handler 应该回答的问题

| 问题 | 为什么重要 |
|---|---|
| 参数缺失时如何报错 | command console 需要可读提示 |
| node path 不存在时如何报错 | 自动化测试需要稳定失败信息 |
| 成功后返回什么 | UI 和 API 可以展示同一结果 |
| 是否可撤销 | editor 行为要符合作者预期 |
| 是否影响保存 | scene 文件 round-trip 要明确 |

## 当前要主动保持一致的地方

当前没有独立的 command metadata 注册 API。新增命令时，我们要手工保持 handler、completion、help 文案、API/MCP 可观察状态和测试一致。这个成本是真实存在的，教程不能把它说成已经被 registry 自动解决。

## 我们已经学会了什么

我们学到新增 command 的第一原则：先把共享行为放进 command bus，再让 UI、API 和测试复用它。

## 下一步

进入 [04 手工增加一个 toolbar 按钮](04-add-toolbar-button-current-path.md)，把 command 接到一个可见按钮上。

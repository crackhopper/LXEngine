# 未来扩展注册表：用 metadata 统一按钮和命令

未来的 editor extension registry 像一张遥控器说明书：每个 command 写清楚名字、参数和是否可撤销；每个 toolbar action 写清楚按钮、图标和点击后 dispatch 的命令。这样 UI、补全、API 和测试都能读同一份说明书。

> 这一章描述的是未来教程目标，由 [REQ-042-b](../../requirements/042-b-tutorial-editor-extension-registry.md) 跟踪。当前仓库还没有完整 command / toolbar metadata 注册入口。

## 未来 command metadata

```yaml
commands:
  - verb: scene                         # -> Command metadata verb
    summary: Mark a node for debug draw # -> help / API schema
    args:                               # -> completion and validation
      - name: subcommand
        enum: [mark-debug]
      - name: nodePath
        type: scene-node-path
    undoable: true                      # -> history / undo policy
    source: extension                   # -> builtin or extension
```

这份 metadata 不替代 C++ handler。它像菜单牌：告诉 editor 这道菜叫什么、需要什么参数、能不能撤销；真正做菜的还是 handler。

## 未来 toolbar action metadata

```yaml
toolbarActions:
  - id: scene.markDebug                 # -> stable action id
    label: Mark                         # -> button text or tooltip
    icon: bug                           # -> optional UI icon name
    group: scene                        # -> toolbar group
    commandTemplate: scene mark-debug ${selection.primary}
    enabledWhen: selection.primary != null
```

未来教程里，新增按钮会变成两步：先注册 command，再注册 toolbar action。按钮点击只负责展开 `commandTemplate` 并 dispatch。

## 未来验证清单

| 验证 | 预期 |
|---|---|
| command console 输入前缀 | completion 来自 metadata |
| 点击 toolbar 按钮 | dispatch 同一条 command |
| HTTP/MCP 查询 schema | 能看到 command 与 toolbar action |
| undo/redo | 使用 metadata 声明的策略 |
| 录制回放 | 记录 command，而不是记录 UI 细节 |

## 当前能提前练习什么

| 当前练习 | 对未来能力的帮助 |
|---|---|
| 从 toolbar 找 dispatch 调用 | 理解按钮与命令的边界 |
| 从 command 找 handler | 理解行为集中在哪里 |
| 从 API 服务看 snapshot | 理解自动化观察入口 |

## 我们已经学会了什么

我们把未来 editor 扩展拆成 command metadata、toolbar action metadata、handler 和 schema 查询四个角色。

## 下一步

继续进入 [扩展场景节点](../extend-scene-node/index.md)，学习新节点如何兼容选择、保存、调试绘制和 command 操作。

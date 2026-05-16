# 手工增加一个 toolbar 按钮：给常用命令装上遥控器

Toolbar 按钮像遥控器上的快捷键。它本身不应该保存业务规则，而是把点击转换成 command。这样同一个行为可以从按钮、console、脚本和 MCP 入口触发。

## 当前 toolbar 在哪里

| 文件 | 作用 |
|---|---|
| `src/demos/lxe_editor/ui_overlay.hpp` | 保存 toolbar 相关状态与绘制入口 |
| `src/demos/lxe_editor/ui_overlay.*` | 绘制浮动 toolbar，处理按钮点击 |
| `src/core/editor/viewport_overlay.cpp` | 部分 viewport overlay 行为也会 dispatch command |
| `src/demos/lxe_editor/lxe_editor_api_service.cpp` | 暴露 toolbar snapshot 给 API |
| `notes/subsystems/scene.md` | 记录当前 toolbar 与 command-first 设计 |

当前 toolbar 已经承担 selection mode、camera controls、preview 等入口。新增按钮时，我们要检查它是否影响 toolbar snapshot、配置保存和 API 观察结果。

## 一个按钮的最小设计

| 项 | 示例 | 说明 |
|---|---|---|
| 显示 | `Mark` | UI 上的短标签 |
| command | `scene mark-debug <selection>` | 点击后 dispatch |
| enabled 条件 | 有选中节点 | 没有目标时按钮禁用 |
| 反馈 | command result | 复用 command 的成功或失败提示 |
| 状态 | optional | 如果按钮表示模式，需要能从 editor state 读回 |

按钮设计要短，行为说明要在 command 里完整。这样以后即使换 UI，命令仍然可用。

## 当前手工修改点

| 修改点 | 目的 |
|---|---|
| toolbar 绘制函数 | 添加按钮和禁用状态 |
| command dispatch | 点击时调用 `CommandBus::dispatch` |
| toolbar snapshot | API/MCP 需要观察新状态时补充字段 |
| editor config | 按钮影响布局或可见性时确认保存逻辑 |
| 测试 / 录制 | 用 command 或 API 验证点击后的状态 |

## 当前路径的风险

| 风险 | 收束方式 |
|---|---|
| 按钮直接改状态 | 改为 dispatch command |
| UI 和 console 行为不同 | 让两者共用 handler |
| API 看不到按钮状态 | 补齐 toolbar snapshot |
| 补全没有新命令 | 同步 completion |

## 我们已经学会了什么

我们知道 toolbar 扩展的重点不是画按钮，而是保证按钮复用 command-first 行为。

## 下一步

进入 [05 未来扩展注册表](05-future-extension-registry.md)，看 metadata 如何让 command、toolbar、API 自动对齐。

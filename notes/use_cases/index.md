# Use Cases

Use case 文档像一张给 agent 的操作卡：它不讲概念，也不替代自动化测试，而是把一次可重复的业务验证写成“前置条件、步骤、验收标准和失败排查”。当我们需要让 Codex 通过 `lxe_manager` MCP 驱动远端 `lxe_editor` 时，先读这里比临时口头编排更稳定。

## 当前场景

| Use case | 适用时机 | 验收重点 |
|---|---|---|
| [录制一次复杂场景编辑](lxe_editor/record-complex-scene-edit.md) | 需要验证 CommandBus、camera、pick、selection、primitive 编辑和 recording/replay 是否串起来 | 录制文件保存、steps 可读、replay 成功、probe 能读 summary/selection/cameras |
| [验证 PBR IBL Helmet 场景](lxe_editor/verify-pbr-ibl-helmet.md) | 需要远端验证 Helmet neutral IBL 场景和 HDR dump 链路 | scene runtime 加载、`hdr.color` dump 非空、Helmet 可见且不是纯黑/纯白 |

## 和其它文档的分工

| 文档类型 | 负责什么 |
|---|---|
| [Tutorial](../tutorial/index.md) | 面向人类学习，解释为什么这样做和怎样一步步理解系统 |
| Use Cases | 面向 agent 执行，强调稳定步骤、可观察状态和验收标准 |
| `src/test/` | 面向自动化回归，直接断言代码行为 |
| [Debug 复盘](../debug/index.md) | 面向事后分析，保留复杂问题的症状、根因和修复过程 |

新增 use case 时，应优先写成业务目标和验收条件，而不是一串没有上下文的命令。命令可以随着 editor API 调整而更新，但 use case 的意图应保持稳定。

# 节点操作合同：新语义要兼容整套 editor

当前没有稳定的 custom node kind registry。新增一种节点语义时，真正要维护的是 editor 对节点的操作合同：能保存、能重建、能选中、能移动、能复制、能调试、能被 API 看见。

## 当前合同

| 操作 | 新节点语义需要保证什么 |
|---|---|
| create | 有明确创建入口，初始 transform 和 payload 可预测 |
| select | path、scene tree、picking 都能定位同一 runtime node |
| move / rotate / scale | 复用 `SceneNode` transform，不绕开通用变换路径 |
| rename | path、scene tree、command 和 API 状态同步刷新 |
| duplicate / copy-paste | payload 复制语义明确，不共享不该共享的临时状态 |
| remove | runtime、document、selection 和 undo/redo 状态清理一致 |
| save / load | scene document 能 round-trip，runtime 不依赖临时 UI 状态 |
| debug draw | 非渲染节点有 bounds 或 helper，便于选择和诊断 |
| API summary | 自动化能识别关键状态，录制回放不依赖 UI 细节 |

## 手工扩展顺序

| 顺序 | 先看哪里 | 为什么 |
|---|---|---|
| 1 | scene document payload | 先确定可保存的权威形状 |
| 2 | runtime 构建 | 确认 document 能变成 runtime node |
| 3 | command / Inspector | 作者能创建和修改 payload |
| 4 | debug draw / picking | editor 能看见、点中和诊断 |
| 5 | duplicate / undo / API | 工具链和自动化不会出现分叉 |

这个顺序比先写 UI 更稳：UI 是入口，document 和 runtime 才是状态闭环。

## 为什么现在不讲 registry

旧教程曾把 node kind registry 当成 future path 链接到过期需求，这会误导读者以为当前项目的下一步是寻找某个 metadata API。当前代码事实不是这样。当前可靠路径是沿着 scene document、runtime、CommandBus、Inspector、DebugDraw、picking 和 API summary 逐项对齐。

如果以后重新设计 node kind metadata，它也应该是对这张合同表的归纳，而不是替代当前 scene/runtime 基础模型。

## 我们已经学会了什么

我们把“新增节点”从一个 C++ 类型拆成了一组 editor 操作合同。只要这些合同没有闭合，新节点就还不能算真正进入 editor。

## 下一步

回到 [Tutorial 总览](../index.md)，按需要复习任一系列，或进入 [场景系统](../../scene-system/index.md) 阅读当前实现细节。

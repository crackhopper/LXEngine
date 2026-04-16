# 引擎循环

这篇文档面向使用者，解释 `EngineLoop` 为什么存在、它和 scene / renderer / pipeline 的边界是什么，以及一帧是怎么被稳定编排出来的。

## 你会在什么场景接触它

- 你想把一个 `Scene` 真正跑起来，而不是手写零散 while-loop。
- 你想知道业务更新、dirty 上传、draw 之间的顺序为什么不能随便调换。
- 你在调试“什么时候要 rebuild scene、什么时候只需要 update 数值”。

## 它负责什么

`EngineLoop` 是 renderer 之上的运行时编排层。它主要负责：

- 组织 `startScene(scene)` 与 `tickFrame()` 两类入口。
- 让 update hook、`uploadData()`、`draw()` 按稳定顺序执行。
- 把结构性变化和普通 dirty 更新拆开，避免每帧偷偷重建 scene。

它不负责：

- 决定某个对象属于哪个 pass，这属于 [场景对象](scene/index.md) 和 [材质系统](material/index.md)。
- 决定 pipeline 身份或 pipeline 构建，这属于 [渲染管线](pipeline/index.md)。
- 执行 Vulkan backend 细节。

## 当前实现状态

- 已实现：`EngineLoop` 作为运行时编排层的基本职责已经存在，详细实现看设计层文档。
- 已实现：场景启动与每帧执行被拆成两段，而不是让 demo 自己把所有事揉进一条 while-loop。

## 为什么它重要

一帧的推荐顺序不是随意排的，而是：

1. 推进时钟。
2. 执行业务层 update hook。
3. 上传 dirty 资源。
4. 执行 draw。

这条顺序把“业务更新”和“渲染执行”切开了。scene、material、camera、light 的 CPU 真值先更新，再进入上传和渲染。pipeline 相关结构如果需要重建，也应该通过显式的 scene rebuild 路径处理，而不是混进普通每帧更新。

## 与其他概念的关系

- 和 [场景对象](scene/index.md)：`EngineLoop` 接管 scene 的生命周期，但不接管 scene 结构本身。
- 和 [相机系统](camera/index.md) / [光源系统](light/index.md)：它决定这些系统的每帧更新时机。
- 和 [渲染管线](pipeline/index.md)：它负责让 pipeline 所依赖的 scene 结构在正确阶段建立，而不是亲自参与 key 或 build desc 计算。

## 继续阅读

- 架构总览：[`../architecture.md`](../architecture.md)
- EngineLoop 设计层文档：[`../subsystems/engine-loop.md`](../subsystems/engine-loop.md)

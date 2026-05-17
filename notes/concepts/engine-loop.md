# 一帧是怎样被组织起来的

这篇文档讨论的重点不是 while-loop 本身，而是当前项目为什么需要 `EngineLoop`，以及它在运行时到底帮我们组织了什么。

## `EngineLoop` 在这里扮演什么角色

`EngineLoop` 是 renderer 之上的运行时编排层。

它不关心 Vulkan 细节，也不负责定义 scene 的结构。它负责的是另一件更实际的事：把“开始一个场景”和“执行一帧”整理成稳定入口。

所以它更像运行时总调度，而不是某个渲染子模块。

## 这一层解决什么问题

如果没有 `EngineLoop`，每个 demo 都会自己写一套 while-loop：

```cpp
while (running) {
    updateScene();
    renderer->uploadData();
    renderer->draw();
}
```

这不是不能跑，但很容易把几类不同性质的工作揉在一起：

- 哪些事只在场景启动时做一次
- 哪些事每帧都做
- update 和 upload / draw 的先后顺序该怎么约束
- 什么时候需要 rebuild scene，而不是只改 dirty 数据

`EngineLoop` 的价值，就是把这些边界固定下来。

## 日常使用里的入口形状

在日常接入里，最重要的是这几个入口：

- `startScene(scene)`
- `setUpdateHook(...)`
- `tickFrame()`
- `run()`
- `requestSceneRebuild()`

也就是说，我们不需要每次都重新发明主循环，只需要把 scene 和每帧更新逻辑接进来。

## 当前代码已经走到哪一步

这套运行时编排语义已经是当前工程的一部分了：

- 场景启动和每帧执行已经是两段式
- update、upload、draw 的顺序已经被明确分开

所以这里不是在讲“未来应该怎样做”，而是在解释“当前这套运行时为什么这样组织”。

## 它怎样和别的系统配合

`EngineLoop` 自己不生产渲染数据，它主要协调别的系统：

- [场景系统](../scene-system/index.md)：提供当前 scene、节点、组件、相机和光源结构
- [材质系统](material/index.md)：普通参数更新在每帧路径里推进
- [材质系统里的 Pipeline 说明](material/what-is-pipeline.md)：需要结构性重建时，应该走显式 rebuild，而不是混入普通 update

## 往实现层再走一步

从底层看，最重要的不是“它有个 loop”，而是它把一帧拆成了稳定顺序：

1. 推进时钟
2. 执行业务 update
3. 上传 dirty 资源
4. 执行 draw

这个顺序把“业务更新”和“渲染执行”分开了，也把“普通数值变化”和“结构性变化”分开了。

继续展开时，可以参考：

- [架构总览](../concepts-design/architecture.md)
- [`../subsystems/engine-loop.md`](../subsystems/engine-loop.md)

# RenderWorkQueue 已退役

`RenderWorkQueue` 曾经是每个 pass 内的任务箱，把 scene renderable 或 offline job 收敛成旧工单。REQ-073-e2 hard cut 后，对应的 `src/core/frame_graph/render_queue.hpp/.cpp` 已删除，正向渲染路径不再使用这个类型。

当前工作流请阅读：

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler
  -> RenderInput[] payloads
  -> RenderInputDesc[] validation/pipeline/binding facts
  -> Vulkan pipeline/upload/execute
```

## 继续阅读

- [RenderWorkCompiler：FramePass 之后的唯一工单编译器](render-work-compiler.md)
- [RenderPathGraph：渲染路线说明书](render-path-graph.md)
- [Realtime 与 Offline：共享同一条 compiler 主线](realtime-offline-shared-flow.md)

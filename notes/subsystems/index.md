# 子系统总览：从运行时编排走到 GPU 提交

子系统文档像一张维护值班表：概念页告诉我们“城市怎么规划”，源码分析告诉我们“某条街的施工细节”，而子系统页回答“今天如果这块出问题，应该先找谁”。这里放的是当前实现里已经形成稳定职责边界的几个维护入口。

## 先按数据流阅读

最稳的阅读顺序是从外层编排进入场景事实，再沿着 render input、资源上传、pipeline cache 走到 Vulkan backend：

| 顺序 | 子系统 | 先回答的问题 |
|---|---|---|
| 1 | [Engine Loop](engine-loop.md) | 一帧什么时候推进，update hook、upload 和 draw 的顺序是什么 |
| 2 | [Scene](scene.md) | scene、node、component、resource table 怎样提供全场景事实 |
| 3 | [Geometry](geometry.md) | mesh、vertex layout、index buffer 怎样进入 draw input 和 pipeline desc |
| 4 | [Resource Upload](resource-upload.md) | CPU 侧资源什么时候同步到 Vulkan resource manager |
| 5 | [Pipeline Cache](pipeline-cache.md) | `RenderInputDesc.pipelineBuildDesc` 怎样变成可复用 pipeline |
| 6 | [Vulkan Backend](vulkan-backend.md) | compiled graph、upload plan、descriptor 和 command buffer 怎样提交到 GPU |
| 7 | [Skeleton](skeleton.md) | skinned pass 需要的 `Bones` UBO 怎样作为独立资源进入 descriptor |

这个顺序不是模块依赖图的全部，而是调试路径。我们先确认帧循环有没有触发，再确认 scene 有没有提供正确事实，然后再看资源、pipeline 和 backend。

## 子系统和概念页的分工

| 如果我们想理解 | 优先读 | 原因 |
|---|---|---|
| 一个概念为什么存在 | `notes/concepts/` 或 `notes/concepts-design/` | 这些页面偏教学，先讲问题和心智模型 |
| 当前代码边界在哪里 | 本目录的子系统页 | 这些页面偏维护，列出职责、约束和修改入口 |
| 某个类型的源码细节 | `notes/source_analysis/` | 这些页面贴着源码注释和真实类型读 |
| 用户可见工作流 | `notes/tutorial/` 或 `notes/scene-system/` | 这些页面从 editor、scene YAML 或命令入口讲起 |

## 常见排查入口

| 现象 | 先看哪里 |
|---|---|
| editor 每帧顺序、scene rebuild 或 update hook 位置不清楚 | [Engine Loop](engine-loop.md) |
| scene 打开后对象、camera、light 或 IBL 资源没有进入渲染 | [Scene](scene.md) |
| 某个 mesh 不参与 pass，或 vertex layout / topology 与 shader 不匹配 | [Geometry](geometry.md) |
| 材质、纹理、IBL、offline storage 没有同步到 backend | [Resource Upload](resource-upload.md) |
| pipeline miss、pipeline key 或 graphics/compute desc 不符合预期 | [Pipeline Cache](pipeline-cache.md) |
| Vulkan 初始化、descriptor、shadow、offline compute 或 viewport 行为异常 | [Vulkan Backend](vulkan-backend.md) |
| skinned mesh 报缺 `Bones` 或 skeleton 与 shader variant 不一致 | [Skeleton](skeleton.md) |

## 继续阅读

- [概念与设计](../concepts-design/index.md)
- [场景系统](../scene-system/index.md)
- [渲染管线](../concepts-design/rendering-pipeline/index.md)
- [源码分析](../source_analysis/index.md)

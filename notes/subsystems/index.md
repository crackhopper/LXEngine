# 子系统文档

> `notes/subsystems/` 是当前系统设计文档入口。

## 怎么读

1. 先看 [Engine Loop](engine-loop.md)、[Frame Graph](frame-graph.md) 和 [Scene](scene.md)，理解“开始场景”与“执行一帧”的边界。
2. 再看 [Pipeline Identity](pipeline-identity.md) 和 [Pipeline Cache](pipeline-cache.md)，理解 pipeline 如何被识别和复用。
3. 最后按需深入 [Shader System](shader-system.md)、[Material System](material-system.md)、[Vulkan Backend](vulkan-backend.md)。
4. 如果你在追踪“CPU 资源什么时候真正上传到 GPU”，先看 [资源上传](resource-upload.md)。
5. 如果要系统理解 Vulkan 后端实现，继续进入 [notes/vulkan-backend/index.md](../vulkan-backend/index.md) 这一组分模块文档。

## 文档地图

- [Engine Loop](engine-loop.md)：场景生命周期、每帧 update hook、run/stop/rebuild
- [Frame Graph](frame-graph.md)：pass 组织、queue 构建、pipeline 预收集
- [Scene](scene.md)：scene 容器、renderable、scene-level 资源
- [Pipeline Identity](pipeline-identity.md)：`PipelineKey`、`PipelineBuildDesc`、pipeline signature
- [Pipeline Cache](pipeline-cache.md)：预构建、查找、运行时 miss
- [Shader System](shader-system.md)：GLSL 编译、SPIR-V 反射、`CompiledShader`
- [Material System](material-system.md)：材质模板、材质实例、反射驱动 UBO
- [Resource Upload](resource-upload.md)：`IGpuResource` 如何进入 renderer / resource manager 的同步路径
- [Geometry](geometry.md)：mesh、vertex layout、topology
- [Skeleton](skeleton.md)：骨骼资源、`SkeletonData`
- [String Interning](string-interning.md)：`GlobalStringTable`、`StringID`、compose
- [Vulkan Backend](vulkan-backend.md)：后端总览与分模块文档入口

## 阅读原则

- 这里写“当前怎么工作”，不写历史方案。
- 权威约束看 `openspec/specs/`。
- 需要实现细节时再跳到 `src/`。

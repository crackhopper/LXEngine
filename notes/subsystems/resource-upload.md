# 资源上传

资源上传回答一个问题：CPU 侧的 mesh、material、camera、light、IBL bake 和 offline storage 事实什么时候变成 Vulkan 资源。当前输入边界是 `RenderInput[]` + accepted `RenderInputDesc[]`，不是旧 work item。

## 当前上传链路

```text
Scene / SceneResourceTable / MaterialInstance
  -> RenderWorkCompiler::buildInputs()
  -> RenderWorkCompiler::prepare()
  -> buildRenderUploadPlan(inputs, descs)
  -> VulkanResourceManager::syncResource(...)
  -> descriptor / pipeline / command recording
```

`buildRenderUploadPlan(...)` 只收集 accepted desc 的 resource dependencies、binding plan descriptors，以及 typed input 自身需要的 vertex/index/storage/output 资源。Rejected desc 只保留诊断，不进入上传计划。

## 两个固定时机

| 时机 | 作用 |
|---|---|
| `initScene()` | 构建 graph/context，准备首帧所需资源和 pipeline |
| `uploadData()` | 在每帧 update hook 后同步 dirty resources |

Resource manager 的判断保持简单：第一次看到资源就创建并上传，已存在且 dirty 就更新，已存在且不 dirty 就跳过。

## 资源身份

`VulkanResourceManager` 当前按 `IGpuResource::getBackendCacheIdentity()` 做 cache key。资源不再只靠对象地址表达身份；显式 cache identity 让 renderer 可以跨包装对象识别同一 backend resource。

活跃资源会进入本轮 active set。短时间漏同步不会立刻销毁，但长期不用的 GPU 资源仍会被 `collectGarbage()` 回收。

## Buffer 与 Texture

| 类型 | 当前策略 |
|---|---|
| Vertex / Index buffer | 创建 Vulkan buffer，按 dirty 协议更新 |
| Uniform / Storage buffer | 由具体 `IGpuResource` 提供字节和 binding 名 |
| CombinedTextureSampler | texture + sampler 成对进入 descriptor |
| Cubemap / mip / layer texture | IBL bake 和 skybox 路径按 texture metadata 创建 |
| Offline output/readback | offline graph executor 创建 output buffer/image 并 readback |

Buffer 路径优先简单正确；texture 路径使用 staging，但仍是同步提交。后续异步上传和驻留策略需要单独设计。

## 从哪里改

| 想改什么 | 入口 |
|---|---|
| upload plan 收集规则 | `src/core/frame_graph/render_upload_plan.*` |
| compiler 资源依赖 | `src/core/frame_graph/render_work_compiler.*` |
| GPU resource cache | `src/backend/vulkan/details/resource_manager.*` |
| texture/buffer 创建 | `src/backend/vulkan/details/device_resources/` |
| scene typed upload view | `src/core/scene/scene_resource_table.*` |

## 关联文档

- [IGpuResource](../source_analysis/src/core/rhi/gpu_resource.md)
- [Vulkan Backend](vulkan-backend.md)
- [RenderWorkCompiler](../concepts-design/rendering-pipeline/render-work-compiler.md)

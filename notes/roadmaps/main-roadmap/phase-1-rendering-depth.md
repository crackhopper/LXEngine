# Phase 1 · Rendering Depth：v0.1.1 FrameGraph / Shadow / CSM

> v0.1.1 目标：让引擎真正支持 FrameGraph v1、directional shadow map 和 CSM。HDR/Post、PBR 完整管线、G-Buffer/Deferred、Task-based pass build 先保留为 pending，不进入当前 active requirements。

## 关键决策

我们先做 **FrameGraph v1**，再做 task-based 并行。

这不是因为并行不重要，而是因为 shadow 和 G-Buffer 的第一性问题是“资源如何从一个 pass 流到另一个 pass”。如果没有稳定的 `FrameGraphResource`、offscreen attachment、读写声明和 barrier 边界，CPU task 并行只能并行地生成一堆仍然无法正确同步的工作。

## 当前代码事实

| 能力 | 当前状态 |
|---|---|
| `Pass_Forward` / `Pass_Shadow` / `Pass_Deferred` / `Pass_DebugOverlay` | 常量存在 |
| `MaterialTemplate` 多 pass | 已支持 |
| validated pass data + scene descriptor resolver | 已支持 pass-aware 资源组装 |
| `SceneNode` pass-level validation | 已支持 |
| `RenderWorkQueue::build(context, pass, target, renderPathNodeSignature, ...)` | 已支持 |
| `FrameGraph` | 只有 `vector<FramePass>` + 顺序构建 queue + pipeline build desc 去重 |
| `RenderTarget` | 只有 format/sample 描述，没有实际 attachment 资源 |
| Vulkan pass 执行 | 当前在单个 swapchain render pass 内循环所有 queue |
| Shadow map / CSM | 未实现 |
| HDR scene color / tone map / Bloom | 未实现 |
| G-Buffer / deferred | 未实现 |
| Task-based pass build / command recording | 未实现 |
| Async compute / timeline semaphore 正式抽象 | 未实现，当前 Vulkan 仍用 fence + binary semaphore |

## v0.1.1 已归档实施顺序

| 顺序 | REQ | 主题 | 说明 |
|---|---|---|---|
| 1 | [REQ-042-a](../../requirements/finished/042-a-frame-graph-v1-resource-target-pass-execution.md) | FrameGraph v1 基础 | `RenderTargetDesc`、offscreen target、resource 声明、同 queue barrier、pass execution |
| 2 | [REQ-042-b](../../requirements/finished/042-b-directional-shadow-map-depth-pass.md) | Directional shadow map | 单 directional shadow map + depth-only pipeline |
| 3 | [REQ-042-c](../../requirements/finished/042-c-cascaded-shadow-maps.md) | Cascaded Shadow Maps | 让 directional shadow 可用到大场景 |
| 4 | [REQ-043-a](../../requirements/finished/043-a-shadow-era-tutorial-support.md) | Shadow 阶段教程支撑 | 完成 shadow/CSM 后补教程场景和说明 |
| 5 | [REQ-043-b](../../requirements/finished/043-b-architecture-concepts-mermaid.md) | 架构概念文档展开 | 用 Mermaid 和模块归属表解释 v0.1.1 后的架构 |

## v0.1.1 后的 pending 队列

| 主题 | 后置原因 |
|---|---|
| HDR Scene Color Target | 需要 FrameGraph v1，但不是 shadow/CSM 截止目标 |
| PostProcess Pass 架构 | 依赖 HDR scene color；先不进入 active |
| PBR 完整管线 | 会扩大 shading/IBL/贴图范围，等 shadow/CSM 稳定后再排 |
| IBL 资源与预过滤 | 属于 PBR/asset pipeline 交叉内容，后置 |
| Bloom / FXAA | 属于 post-process 链路，后置 |
| G-Buffer / Deferred | 是下一轮多 pass 大功能，v0.1.1 不同时引入 |
| Frustum culling | 可独立优化，但不是 shadow/CSM 前置 |
| Task-based pass build | 等 FrameGraph 产出稳定 pass work units 后再并行 |

## REQ-042-a · FrameGraph v1 基础

这是 shadow 和 G-Buffer 的共同前置。

| 子项 | 要做什么 | 不做什么 |
|---|---|---|
| `RenderTargetDesc` | 描述 format、extent policy、sample count、attachment kind、load/store | 不直接持有 GPU image |
| `RenderTarget` | 持有 desc + backend attachment handles 或 core 侧资源句柄 | 不进入材质系统 |
| `FrameGraphResource` | 命名 pass 输出/输入，如 `shadow.depth`, `scene.hdrColor` | 不做 aliasing |
| `FramePass` 扩展 | 声明 reads/writes、target/depth、queue 顺序 | 不做 JSON 数据驱动 |
| `compile()` v1 | 校验资源存在、按声明顺序生成执行计划、发现明显 cycle | 不做复杂拓扑优化 |
| Vulkan 执行 | 每个 pass 使用正确 render pass/framebuffer 或 dynamic rendering 等价封装 | 不做 multi-queue |
| Barrier v1 | 同 graphics queue 内的 image layout transition / access mask | 不做跨 queue ownership |
| Pipeline identity | 把 target/pass 关键结构纳入 pipeline 兼容性设计 | 不做 bindless 重写 |

**验收**：构建两个 pass：A 写一张 offscreen color texture，B fullscreen 采样 A 并写 swapchain。RenderDoc 能看到正确的 image layout 转换。

## REQ-101 · HDR Scene Color Target

> Pending。v0.1.1 不实施；保留旧 heading anchor 供研究文档引用。

FrameGraph v1 完成后，`Forward` 不再直接写 swapchain，而是写 HDR scene color：

- 增加 `RGBA16F` 或等价 HDR color format。
- `Forward` pass 写 `scene.hdrColor` + depth。
- `PostProcess` pass 读 `scene.hdrColor`，写 swapchain。
- pipeline build desc 能区分 HDR target 和 swapchain target。

**验收**：同一场景经 HDR target → tone map 输出，亮度范围和 gamma 正常。

## REQ-102 · PostProcess Pass 架构

> Pending。v0.1.1 不实施；FrameGraph / Shadow / CSM 稳定后再重新排序。

- Fullscreen triangle，无 vertex buffer。
- `Pass_PostProcess` 或等价 pass 名。
- Tone map shader：Reinhard + ACES。
- 后续 Bloom / FXAA 复用同一 fullscreen pass 设施。

**验收**：开关 tone map 算法，画面亮度变化可见且 pipeline cache 稳定。

## REQ-103 · Shadow Pass + Depth-only Pipeline

> Finished 映射：[REQ-042-b](../../requirements/finished/042-b-directional-shadow-map-depth-pass.md)。本 heading 保留旧 anchor 供研究文档引用。

Shadow 是第一个真正验证多 pass 的功能：

```text
Shadow pass:
  writes shadow.depth

Forward pass:
  reads shadow.depth
  writes scene.hdrColor
```

工作项：

- `Pass_Shadow` 使用 depth-only target。
- shadow caster 材质增加 `Shadow` pass，或提供可复用 depth-only shader。
- Directional light 提供 light view-projection。
- Forward/PBR shader 采样 shadow map。
- 先实现 hard shadow，再加 3x3 PCF。

**验收**：地面上有方块阴影；移动 directional light 或物体后阴影随之变化。

## REQ-104 · Cascaded Shadow Maps

> Finished 映射：[REQ-042-c](../../requirements/finished/042-c-cascaded-shadow-maps.md)。本 heading 保留旧 anchor 供研究文档引用。

单 shadow map 只能证明链路，CSM 才能让方向光阴影在测试场景里可用：

- 4 cascade split。
- 每 cascade 一张 depth layer 或 texture array layer。
- scene/camera 计算 cascade VP。
- fragment 按 view depth 选择 cascade。
- cascade 边界 debug view。

**验收**：近处阴影清晰，远处不明显闪烁；debug overlay 能显示 cascade 范围。

## REQ-105 · Environment Map Loader + Scene-level IBL 资源

> Pending。v0.1.1 不实施；PBR/IBL 路线后续再重新排序。

- HDR equirectangular / cubemap 加载。
- `Scene` 持有 environment / irradiance / prefilter / BRDF LUT。
- IBL 作为 scene-level resource 注入 PBR pass。
- 不把 IBL 建模成 `LightBase`。

**验收**：关掉直接光后，PBR 物体仍有环境照明。

## REQ-106 · IBL Prefilter

> Pending。v0.1.1 不实施；PBR/IBL 路线后续再重新排序。

- Diffuse irradiance cube。
- Specular prefilter mip chain。
- BRDF LUT。
- 允许先 runtime 生成，后续 Phase 3/12 再做缓存和离线预计算。

**验收**：roughness 改变时反射模糊程度变化正确。

## REQ-107 · Bloom Pass

> Pending。v0.1.1 不实施；PostProcess 路线后续再重新排序。

- bright pass。
- downsample / upsample chain。
- 在 tone map 前合成回 HDR scene color。

**验收**：高亮区域有可控柔光，不污染整屏。

## REQ-108 · FXAA Pass

> Pending。v0.1.1 不实施；PostProcess 路线后续再重新排序。

- tone map 后 LDR 阶段执行。
- 单 fullscreen pass。

**验收**：高对比几何边缘锯齿减少。

## REQ-109 · PointLight + SpotLight + 统一多光源合同

> Pending。对象模型已具备基础，剩余 shading 工作不进入 v0.1.1 active。

`PointLight` / `SpotLight` 对象模型和 `SceneLightsUBO` 已经落地。当前 REQ 在 Phase 1 中只保留剩余渲染工作：

- Forward/PBR shader 消费 `SceneLightsUBO`。
- 方向光、点光、聚光使用统一 shading 函数。
- 超出 `MaxDirectionalLights` / `MaxPointLights` / `MaxSpotLights` 时规则稳定。
- 多光源数量变化不改变 `PipelineKey`。

**验收**：3 点光 + 1 聚光 + 1 方向光在 PBR/BlinnPhong 路径中亮度合计正确。

## REQ-118 · PBR 完整管线

> Pending。v0.1.1 不实施；避免和 Shadow / CSM 同时扩大材质与光照范围。

当前 PBR 只是最小材质示例。本 REQ 把它接成可用路径：

- 多光源循环。
- shadow 接收。
- IBL ambient。
- albedo / normal / metallicRoughness / occlusion / emissive 贴图集。
- 保留 forward PBR 作为 deferred 前的 reference。

**验收**：同一 PBR 材质在 direct light + shadow + IBL 下与参考 viewer 接近。

## REQ-119 · G-Buffer / 延迟渲染路径

> Pending。v0.1.1 不实施；作为 CSM 之后的下一轮多 pass 大功能。

G-Buffer 是 shadow 之后的第二个多 pass 大功能：

```text
Geometry pass:
  writes gbuffer.albedoAO
  writes gbuffer.normalRM
  writes gbuffer.emissive
  writes depth

Lighting pass:
  reads gbuffer.*
  reads depth
  reads shadow.depth
  writes scene.hdrColor
```

首版范围：

- `Pass_Deferred` material pass。
- MRT target。
- fullscreen lighting pass。
- scene-level render path 切换：Forward / Deferred。
- 透明物体和 overlay 继续走 forward/debug。

**验收**：同一场景可切 forward/deferred；G-Buffer debug view 可检查 albedo、normal、roughness、depth。

## REQ-110 · Frustum Culling

> Pending。可独立优化，但不是 FrameGraph / Shadow / CSM 前置。

- 使用已有 `SceneNode` world bounds。
- `RenderWorkQueue::build` 在 visibility mask 后加 frustum test。
- 先 CPU culling；GPU-driven culling 后置到研究路线。

**验收**：相机转向空处时 draw item 数明显下降。

## REQ-120 · Task-based Pass Build / Command Recording

> Pending。等 FrameGraph 产出稳定 pass work units 后再并行。

只有在 FrameGraph v1 和至少一个多 pass 功能稳定后，才接 task-based 并行：

| 输入 | 输出 |
|---|---|
| compiled frame graph execution plan | pass work units |
| pass resource dependencies | task dependency edges |
| render queue items | secondary command buffers 或 backend work packets |

首版只做 CPU 侧并行：queue build、pipeline build desc 收集、secondary command buffer recording。Timeline semaphore / multi-queue 仍后置。

**验收**：同一 frame 的独立 pass 或 pass 内 item recording 可以并行；输出命令顺序确定，可 replay。

## Async compute 与 timeline 的位置

当前不把 async compute 放在 shadow 前置。timeline semaphore 的长期模型仍采用研究文档里的方向：timeline 绑定 queue/submit 进度线，资源记录 retire value。但 Phase 1 的 shadow/HDR/G-Buffer 都可以先在 graphics queue 内完成，主要同步需求是 image layout transition 和 same-queue barrier。

## 里程碑

| 里程碑 | 条件 |
|---|---|
| M1.1 FrameGraph v1 | offscreen color pass → fullscreen sample pass |
| M1.2 Shadow | 单 directional shadow + PCF |
| M1.3 CSM | 4 cascade + debug view |
| M1.4 Tutorial | shadow/CSM 教程场景与说明齐备 |
| M1.5 Architecture docs | 架构概念文档能解释 v0.1.1 后的系统和模块归属 |

## 继续阅读

- [Frame Graph 技术调研](../research/frame-graph/README.md)
- [Shadows 技术调研](../research/shadows/README.md)
- [Bindless Texture 技术调研](../research/bindless-texture/README.md)
- [Timeline 与资源退休模型](../research/multi-threading/08-Timeline与资源退休模型.md)

# REQ-003a: 统一 getPipelineHash 约定与通用 Pipeline 类

> 本需求由原 REQ-003（Pipeline 预构建机制）拆分而来。REQ-003a 收录**已落地**的部分；尚未落地的 FrameGraph / PipelineCache / PipelineBuildInfo / ImageFormat+RenderTarget / PipelineSlotId 废除等工作归入 REQ-003b。

## 背景

原 REQ-003 引入了一条设计主线：**所有影响 pipeline 唯一性的资源，通过统一的 `getPipelineHash()` 通道把信息交给 PipelineKey**；同时替换掉 backend 中硬编码的 pipeline 子类（`VkPipelineBlinnPhong`），让 pipeline 构建可由数据驱动。

这部分基础设施已经随 REQ-001 / REQ-002 的落地一并完成，单列归档以便后续引用。

## 需求

### R1: 资源统一提供 getPipelineHash()

每个参与 PipelineKey 的资源类须提供 `size_t getPipelineHash() const`，含义与合并规则如下：

| 类 | getPipelineHash() 内容 |
|----|----------------------|
| `Mesh` | 顶点布局 + 图元拓扑（`hash_combine(layoutHash, topology)`） |
| `RenderState` | 完整渲染状态 hash（委托 `getHash()`） |
| `ShaderProgramSet` | Shader 名 + 变体集合 hash（委托 `getHash()`） |
| `Skeleton` | 固定 tag `kSkeletonPipelineHashTag`（"启用骨骼"维度） |

**约定**：

- 无需 `IPipelineInfluencer` 抽象基类——duck typing 已足够，避免虚函数开销与继承耦合
- 原有 `getHash()` / `getLayoutHash()` 保留不删，`getPipelineHash()` 可直接委托
- 看到 `getPipelineHash()` 即知"此值参与 pipeline 标识"
- "无骨骼"场景由 `SubMesh::skeleton` 是否持有决定，调用方做 `skeleton ? skeleton->getPipelineHash() : 0`，而非让 Skeleton 自己返回一个"无骨骼" hash

### R2: 废除 pipeline 子类

`VkPipelineBlinnPhong` 及同类 shader 专用 pipeline 子类必须删除。Backend 保留单一通用实现：

- `src/backend/vulkan/details/pipelines/vkp_shader_graphics.hpp` 提供 `VulkanShaderGraphicsPipeline`
- 通过 `shaderBaseName + VertexLayout + PipelineSlotDetails + PushConstantDetails + PrimitiveTopology` 创建
- `VulkanResourceManager::getOrCreateRenderPipeline()` 是唯一的工厂入口，按 `PipelineKey` 缓存

> `PipelineSlotDetails` / `PipelineSlotId` 仍被该实现依赖——彻底废除（替换为 shader 反射信息）归入 REQ-003b R4。

## 实施状态（2026-04-13）

| 需求 | 落地位置 | 状态 |
|------|---------|------|
| R1 getPipelineHash 约定 | `core/resources/{mesh,material,shader,skeleton}.hpp` | 已完成（随 REQ-001 R6/R7） |
| R2 废除 pipeline 子类 | `backend/vulkan/details/pipelines/vkp_shader_graphics.{hpp,cpp}` | 已完成 |

## 与其他需求的关系

- **REQ-001 R6/R7**：定义了 `getPipelineHash()` 命名规范与 `PipelineKey::build()` 接收资源对象的签名
- **REQ-002**：定义了 `PipelineKey` 的数据结构、字符串格式与 backend 缓存集成
- **REQ-003b（待落地）**：在 R1/R2 基础上引入 `PipelineBuildInfo`、`PipelineCache` 独立类、`FrameGraph` / `RenderQueue` / `ImageFormat` / `RenderTarget`、预构建流程，以及彻底废除 `PipelineSlotId`

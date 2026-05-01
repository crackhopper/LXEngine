# REQ-003b: Pipeline 预构建机制

> 本需求由原 REQ-003 拆分而来。已完成部分（`getPipelineHash` 约定、通用 `VulkanShaderGraphicsPipeline`、`VkPipelineBlinnPhong` 废除）归档在 `finished/003a-pipeline-hash-and-generic-pipeline.md`。本文件只描述**尚未落地**的工作。

## 背景

REQ-003a 完成后，backend 已经能按 `PipelineKey` 缓存/查找 pipeline，并通过统一的通用 pipeline 类创建。但当前仍存在三类问题：

1. **全部 lazy**：`VulkanResourceManager::getOrCreateRenderPipeline()` 是纯 miss-then-create，首帧会出现大量卡顿；没有加载期一次性预构建入口
2. **Pipeline 构建依赖硬编码 slot 表**：`vk_resource_manager.cpp` 中针对 `shaderName == "blinnphong_0"` 调用 `blinnPhongForwardSlots()` 取 `PipelineSlotDetails`，新增 shader 必须改 backend 代码——这与数据驱动相悖
3. **缺少帧结构抽象**：Scene 仅持有单个 renderable，没有 pass / render queue 的概念，无法扫描"整个场景在所有 pass 下的 pipeline 需求"，预构建无从下手

## 目标

- 定义 `PipelineBuildInfo`：从 `RenderingItem` 汇总 backend 构建 pipeline 所需的**全部数据**（顶点布局、shader 字节码、反射 binding、渲染状态、topology），不再依赖 shader 名字查表
- 抽出独立的 `PipelineCache` 类，与 `VulkanResourceManager` 解耦
- 引入 `ImageFormat` / `RenderTarget` / `RenderQueue` / `FrameGraph` 薄壳，支持"一帧多 pass"建模
- 提供 `FrameGraph::buildFromScene(...)` + `collectAllPipelineBuildInfos()` → `Backend::preloadPipelines(...)` 的加载期预构建链路
- 彻底废除 `PipelineSlotId` / `PipelineSlotDetails` / `forward_pipeline_slots.hpp`，由 shader 反射信息驱动

## 需求

### R1: PipelineBuildInfo

Backend 构建 pipeline 所需的全部信息封装在 `PipelineBuildInfo` 中，从 `RenderingItem` 提取：

```cpp
// src/core/resources/pipeline_key.hpp 或新文件
namespace LX_core {

struct PipelineBuildInfo {
  PipelineKey key;

  // Shader 字节码（所有 stages）
  std::vector<ShaderStageCode> stages;

  // 反射得到的 descriptor binding 信息
  std::vector<ShaderResourceBinding> bindings;

  // 顶点输入
  VertexLayout vertexLayout;

  // 渲染状态
  RenderState renderState;

  // 图元拓扑
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;

  // Push constant range（当前引擎约定）
  PushConstantRange pushConstant;

  static PipelineBuildInfo fromRenderingItem(const RenderingItem &item);
};

}
```

**关键点**：`bindings` 来自 shader 反射（已有 `ShaderReflector` / `ShaderResourceBinding`），完全替代 `PipelineSlotDetails`。`fromRenderingItem()` 负责从 item 取出 mesh / material / shader 并填满字段。

### R2: ImageFormat 与 RenderTarget

core 层不依赖 Vulkan，须自定义格式枚举：

```cpp
// src/core/gpu/image_format.hpp
namespace LX_core {

enum class ImageFormat : uint8_t {
  RGBA8,
  BGRA8,
  R8,
  D32Float,
  D24UnormS8,
  D32FloatS8,
};

}
```

Backend 提供映射：`VkFormat toVkFormat(ImageFormat)`。

```cpp
// src/core/gpu/render_target.hpp
namespace LX_core {

struct RenderTarget {
  ImageFormat colorFormat = ImageFormat::BGRA8;
  ImageFormat depthFormat = ImageFormat::D32Float;
  uint8_t     sampleCount = 1;

  size_t getHash() const;
};

}
```

> **约束**：当前阶段 RenderTarget hash 暂不纳入 PipelineKey（系统中只有一个 forward pass）。保留接口供未来扩展。

### R3: RenderQueue 与 FrameGraph

```cpp
// src/core/scene/render_queue.hpp
namespace LX_core {

class RenderQueue {
public:
  void addItem(RenderingItem item);
  void sort();  // 按 pipelineKey 排序，减少 pipeline 切换

  const std::vector<RenderingItem> &getItems() const;

  /// 收集队列中所有唯一的 PipelineBuildInfo
  std::vector<PipelineBuildInfo> collectUniquePipelineBuildInfos() const;

private:
  std::vector<RenderingItem> m_items;
};

}
```

```cpp
// src/core/scene/frame_graph.hpp
namespace LX_core {

struct FramePass {
  std::string  name;        // "forward", "shadow", ...
  RenderTarget target;
  RenderQueue  queue;
};

class FrameGraph {
public:
  void addPass(FramePass pass);

  /// 从场景扫描所有 renderable，填充各 pass 的 RenderQueue
  void buildFromScene(const Scene &scene);

  /// 收集所有 pass 中的唯一 PipelineBuildInfo（按 PipelineKey 去重）
  std::vector<PipelineBuildInfo> collectAllPipelineBuildInfos() const;

  const std::vector<FramePass> &getPasses() const;

private:
  std::vector<FramePass> m_passes;
};

}
```

Scene 增加扫描接口：

```cpp
class Scene {
public:
  /// 返回场景中所有 renderable（供 FrameGraph 遍历）
  const std::vector<IRenderablePtr> &getRenderables() const;
};
```

> 当前 `Scene` 只持有单个 `IRenderablePtr mesh`，本需求需要将其改为 `std::vector<IRenderablePtr>` 并更新所有调用点。

### R4: PipelineCache 独立类

将现在藏在 `VulkanResourceManager::m_pipelines` 中的缓存抽出为独立类：

```cpp
// src/backend/vulkan/details/pipelines/pipeline_cache.hpp
namespace LX_core::backend {

class PipelineCache {
public:
  explicit PipelineCache(VulkanDevice &device);

  /// 查找已缓存的 pipeline，未命中返回 nullopt
  std::optional<std::reference_wrapper<VulkanPipeline>>
  find(const PipelineKey &key);

  /// 查找或构建（miss 时自动 build 并缓存；打印 warn 日志）
  VulkanPipeline &getOrCreate(const PipelineBuildInfo &buildInfo,
                              VkRenderPass renderPass);

  /// 批量预构建
  void preload(const std::vector<PipelineBuildInfo> &buildInfos,
               VkRenderPass renderPass);

private:
  VulkanDevice &m_device;
  std::unordered_map<PipelineKey, VulkanPipelinePtr, PipelineKey::Hash> m_cache;
};

}
```

`VulkanResourceManager::getOrCreateRenderPipeline()` 改为对 `PipelineCache` 的薄壳委托，最终目标是移除 `VulkanResourceManager` 中的 pipeline 缓存逻辑。

### R5: 通用 Pipeline 构建（驱动化）

`VulkanShaderGraphicsPipeline`（或替代类）改为接收 `PipelineBuildInfo`：

```
VulkanPipeline::build(device, buildInfo, renderPass)
    │
    ├── 1. 创建 ShaderModule（从 buildInfo.stages[].bytecode）
    ├── 2. 配置 VertexInput（从 buildInfo.vertexLayout → VkVertexInputAttributeDescription）
    ├── 3. 创建 DescriptorSetLayout（从 buildInfo.bindings → VkDescriptorSetLayoutBinding）
    ├── 4. 创建 PipelineLayout（descriptor layouts + push constant range）
    ├── 5. 配置固定管线状态（从 buildInfo.renderState 映射到 Vulkan）
    ├── 6. vkCreateGraphicsPipelines()
    └── 7. 销毁 ShaderModule（创建后不再需要）
```

Core → Vulkan 映射由 backend 提供（参见 cpp-style-guide Layer Dependency Rules）。

### R6: 废弃 PipelineSlotId / PipelineSlotDetails

彻底删除下列硬编码：

| 文件/符号 | 原因 |
|----------|------|
| `src/backend/vulkan/details/pipelines/vkp_pipeline_slot.hpp`（`PipelineSlotDetails` / 相关类型） | 由 `ShaderResourceBinding`（反射）替代 |
| `src/backend/vulkan/details/pipelines/forward_pipeline_slots.hpp`（`blinnPhongForwardSlots()`） | 不再需要 shader-name 查表 |
| `PipelineSlotId` 枚举（`src/core/gpu/render_resource.hpp`） | 资源 → slot 的映射改走反射查找 |
| `vk_resource_manager.cpp` 中 `if (shaderName == "blinnphong_0") { ... }` 分支 | 由 `PipelineBuildInfo::fromRenderingItem()` 统一取反射信息 |

> **注意**：`PipelineSlotId` 当前被多个资源类（`SkeletonUBO`、`Camera`、`Light`、`Material`、`Texture` 等）用作 `getPipelineSlotId()` 的返回类型。废除这个枚举会扩散到整个资源层——**此项是本需求最大的侵入点**，实施时应先确定反射-驱动的绑定查找接口，再批量迁移。

### R7: 预构建流程

#### 加载期（主要方式）

```
场景加载完毕 / 场景切换时
    │
    ▼
FrameGraph::buildFromScene(scene)
    │  遍历 scene.getRenderables()
    │  为每个 renderable × 每个 pass 构建 RenderingItem
    │  填入对应 pass 的 RenderQueue
    ▼
FrameGraph::collectAllPipelineBuildInfos()
    │  遍历所有 pass 的 RenderQueue
    │  收集所有 RenderingItem 的 PipelineBuildInfo
    │  按 PipelineKey 去重
    ▼
Backend::preloadPipelines(buildInfos)
    │  for each info: pipelineCache.getOrCreate(info, renderPass)
    ▼
所有 pipeline 就绪
```

#### 运行时按需构建（兜底）

如果渲染时遇到缓存未命中（运行时新增 material），自动构建并输出警告日志：

```
draw(RenderingItem item)
    │
    ├── cache.find(item.pipelineKey)
    ├── hit  → 使用
    └── miss → cache.getOrCreate(...)
              → LOG_WARN("Pipeline cache miss: {}", key.getName())
              → 使用
```

## 数据流全景

```
 ┌─────────── 加载阶段 ─────────────────────────────────────────────────────────┐
 │                                                                               │
 │  GLSL → ShaderCompiler → ShaderReflector → ShaderImpl (IShader)              │
 │                                                                               │
 │  MaterialTemplate.setPass("forward", { renderState, shaderSet })             │
 │                                                                               │
 │  Scene 持有所有 renderable (mesh + material + skeleton)                       │
 │      │                                                                        │
 │      ▼                                                                        │
 │  FrameGraph::buildFromScene(scene)                                           │
 │      │  遍历 renderable × pass                                               │
 │      │  每个组合 → RenderingItem { mesh, material, skeleton, passEntry }     │
 │      │             .pipelineKey = PipelineKey::build(...)                     │
 │      ▼                                                                        │
 │  FrameGraph::collectAllPipelineBuildInfos()                                  │
 │      │  PipelineBuildInfo::fromRenderingItem(item) × N                       │
 │      │  去重 by PipelineKey                                                  │
 │      ▼                                                                        │
 │  Backend::preloadPipelines(buildInfos)                                       │
 │      │  PipelineCache::preload(...)                                          │
 │      │    → VulkanPipeline::build(device, info, renderPass)                  │
 │      │    → 缓存 { PipelineKey → VulkanPipeline }                           │
 │                                                                               │
 └───────────────────────────────────────────────────────────────────────────────┘

 ┌─────────── 渲染阶段 ────────────────────────────────────────┐
 │                                                               │
 │  for each FramePass in frameGraph:                            │
 │    beginRenderPass(pass.target)                               │
 │    for each RenderingItem in pass.queue (sorted by key):      │
 │      pipeline = cache.find(item.pipelineKey)        // O(1)   │
 │      cmd->bindPipeline(pipeline)                              │
 │      cmd->bindResources(item)                                 │
 │      cmd->draw(item)                                          │
 │                                                               │
 └───────────────────────────────────────────────────────────────┘
```

## 边界与约束

- **core 层无外部依赖**：所有 core 对象（`RenderTarget`、`ImageFormat` 等）仅使用 C++ 标准库，backend 提供到 Vulkan 的映射
- **Dynamic state**：Viewport 和 Scissor 保持 dynamic state，不影响 pipeline 唯一性
- **Push constant**：采用引擎约定（128 字节，vertex+fragment stages），所有 pipeline 共享相同 push constant range，不参与 PipelineKey
- **VkPipelineCache**：可选优化项（使用 Vulkan 原生 pipeline cache 加速重启时构建），当前不做要求
- **RenderTarget 对 Pipeline 的影响**：不同 RenderTarget 可能产生不同的 VkRenderPass（color/depth format 差异），`PipelineCache::getOrCreate` 需要配合正确的 VkRenderPass，但 RenderTarget 的 hash 暂不纳入 PipelineKey（当前只有一个 forward pass）

## 建议的实施顺序

为降低侵入风险，建议分 3 个阶段推进：

1. **阶段 1（数据封装）**：R1 `PipelineBuildInfo` + R2 `ImageFormat`/`RenderTarget`——纯增量、零破坏
2. **阶段 2（帧结构）**：R3 `RenderQueue`/`FrameGraph` + `Scene::getRenderables()`——扩展 Scene API，保持旧路径可用
3. **阶段 3（侵入式替换）**：R4 `PipelineCache` 独立类 + R5 驱动化通用 pipeline + R6 废除 `PipelineSlotId` + R7 接入预构建——集中处理破坏性改动，配合完整回归测试

## 依赖

- REQ-001 R6/R7（`getPipelineHash()` 约定 + `PipelineKey::build()` 签名）✅ — 随 REQ-007 被替换为 `getRenderSignature(pass)` 通道
- REQ-002（`PipelineKey` + backend 缓存集成）✅
- REQ-003a（通用 pipeline 类 + shader 子类废除）✅
- **REQ-004（`ShaderResourceBinding.members` 反射）** — R1 `PipelineBuildInfo.bindings` 强依赖：只有反射把 UBO struct 成员暴露出来，PipelineBuildInfo 才能脱离 `PipelineSlotDetails` 的硬编码查表
- **REQ-005（`MaterialInstance` 是唯一 `IMaterial` 实现）** — R3 `FrameGraph::buildFromScene()` 遍历 renderable 时假设 material 由反射驱动
- **REQ-007（结构化 Interning Pipeline Identity）** — R3/R7 的"按 pass 扫描"路径消费 `Scene::buildRenderingItem(StringID pass)` 与 `IRenderable::getRenderSignature(pass)`；本需求的 `FramePass.name` 从 `std::string` 迁移为 `StringID`，与 `Pass_Forward / Pass_Shadow` 常量保持一致
- 现有 `ShaderReflector` / `ShaderResourceBinding`（反射信息完备性）—— R6 强依赖，由 REQ-004 补齐 members

## 实施状态

已完成（2026-04-13）— 通过 openspec 变更 `pipeline-prebuilding` 落地，分三个阶段按 tasks.md 顺序推进，每阶段 build + test 回归后再进入下一阶段。

**阶段 1（纯 additive）**
- `src/core/resources/pipeline_build_info.{hpp,cpp}` — `PipelineBuildInfo` 结构 + `fromRenderingItem` 工厂 + `PushConstantRange` 定义
- `src/core/gpu/image_format.hpp` — `ImageFormat` 枚举
- `src/core/gpu/render_target.{hpp,cpp}` — `RenderTarget` 结构 + 稳定哈希
- Backend `toVkFormat(ImageFormat)` helper（`vk_resource_manager.cpp`）
- `RenderingItem` 新增 `MaterialPtr material` 字段；`Scene::buildRenderingItem` 自动填充
- 测试 `test_pipeline_build_info.cpp` — 7 个用例全部通过

**阶段 2（帧结构）**
- `src/core/scene/render_queue.{hpp,cpp}` — `RenderQueue` + 按 `PipelineKey` 排序去重
- `src/core/scene/frame_graph.{hpp,cpp}` — `FrameGraph` + `FramePass`（`StringID name`，匹配 REQ-007 pass 常量）+ `buildFromScene` / `collectAllPipelineBuildInfos`
- `Scene::m_renderables` 从单字段迁移到 `std::vector<IRenderablePtr>`；新增 `getRenderables()` / `addRenderable` / `buildRenderingItemForRenderable`
- 测试 `test_frame_graph.cpp` — 6 个用例全部通过

**阶段 3（破坏性替换，原子落地）**
- `IRenderResource::getBindingName() const` 新虚函数；`Camera/Light/Skeleton/UboByteBufferResource` 返回 `StringID("CameraUBO"/"LightUBO"/"Bones"/"MaterialUBO")`
- `CombinedTextureSampler` 重构：删 `m_slotId` 字段与构造参数；新增 `setBindingName` + `m_bindingName`，由 `MaterialInstance::getDescriptorResources` 在返回前填入反射绑定名
- **彻底删除**：`enum class PipelineSlotId`、`IRenderResource::getPipelineSlotId`、`vkp_pipeline_slot.hpp`、`forward_pipeline_slots.hpp`、`blinnPhongForwardSlots()`、`blinnPhongPushConstants()`、`vk_resource_manager.cpp` 中的 `shaderName == "blinnphong_0"` 分支
- `VulkanPipeline` 完全重构：构造函数接收 `const PipelineBuildInfo &`；`m_bindings: vector<ShaderResourceBinding>` 替代 `m_slots`；`loadShaders` 从 `m_stages.bytecode` 创建 `VkShaderModule` 而非读文件；`createLayout` 按反射 `set` 字段分组构造 descriptor set layouts；rasterizer / depthStencil / colorBlend / inputAssembly 全部从 `m_renderState` / `m_topology` 推导
- `VulkanShaderGraphicsPipeline::create(device, buildInfo, renderPass, shaderName)` 一次性构造完整 pipeline
- `VulkanDescriptorManager::getOrCreateLayout` / `allocateSet` 签名改为 `vector<ShaderResourceBinding>`；`DescriptorLayoutKey` 对应迁移；hash 按 `(set, binding, type, stageFlags, descriptorCount)` 五元组，忽略 `name` 以允许跨 shader 共享 layout
- `VulkanCommandBuffer::bindResources` 删 `findBySlotId`；改为构造 `unordered_map<StringID, IRenderResourcePtr>` 按反射 binding `name` 匹配
- 新建 `src/backend/vulkan/details/pipelines/pipeline_cache.{hpp,cpp}` 独立类；`VulkanResourceManager::m_pipelines` 迁移到 `m_pipelineCache`；`getOrCreateRenderPipeline(item)` 变成薄壳 delegator；新增 `preloadPipelines(infos)` 与 `getPipelineCache()` 访问器
- `VulkanRenderer::initScene` 增加 `FrameGraph` 构造 + `preloadPipelines` 调用
- 测试 `test_pipeline_cache.cpp` 覆盖 `find` / `getOrCreate` / `preload` / 幂等性（GPU 路径；headless 环境优雅 SKIP）

**验证**
- `grep -rn "PipelineSlotId\|PipelineSlotDetails\|blinnPhongForwardSlots"` 归零
- `openspec validate pipeline-prebuilding --strict` 通过
- 全量 `cmake --build ./build` 绿，0 warning 0 error
- `test_string_table` / `test_pipeline_identity` / `test_pipeline_build_info` / `test_frame_graph` / `test_material_instance` 全部通过
- Vulkan 集成测试 `test_vulkan_*` 与 `test_pipeline_cache` 编译链接通过，headless 环境无显示时按约定 SKIP

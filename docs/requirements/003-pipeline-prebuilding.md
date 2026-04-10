# REQ-003: Pipeline 预构建机制

## 背景

### 当前现状

Backend pipeline 管理完全硬编码：

1. `initializeRenderPassAndPipeline()` 手动创建唯一一个 `VkPipelineBlinnPhong`
2. 每种渲染效果需要写一个 pipeline 子类，硬编码顶点格式、Slot 布局
3. 无法支持 shader 变体自动派生不同 pipeline

### 目标

引入 `IPipelineInfluencer` 接口 + 场景管线对象，实现数据驱动的 pipeline 预构建：

- core 层定义影响 pipeline 的接口和场景结构
- backend 层接收 `RenderingItem`，自动构建和缓存 pipeline
- 场景加载后一次性预构建所有需要的 pipeline

## 需求

### R1: IPipelineInfluencer 接口

定义一个 core 层接口，所有影响 pipeline 构建的资源类须实现：

```cpp
// src/core/gpu/pipeline_influencer.hpp
namespace LX_core {

class IPipelineInfluencer {
public:
  virtual ~IPipelineInfluencer() = default;

  /// 返回此资源对 pipeline 唯一性的 hash 贡献
  virtual size_t getPipelineHash() const = 0;
};

}
```

实现此接口的类：

| 类 | getPipelineHash() 含义 |
|----|----------------------|
| `Mesh` | 顶点布局 + 图元拓扑 |
| `RenderPassEntry` | 渲染状态 + shader 程序（含变体） |
| `Skeleton` | 有无骨骼的布尔标志 |

```cpp
// Mesh
size_t Mesh::getPipelineHash() const {
  size_t h = m_vertexBuffer->getLayout().getHash();
  hash_combine(h, static_cast<uint32_t>(m_indexBuffer->getTopology()));
  return h;
}

// RenderPassEntry — 已有 getHash()，委托即可
size_t RenderPassEntry::getPipelineHash() const {
  return getHash();  // renderState + shaderSet
}

// Skeleton
size_t Skeleton::getPipelineHash() const {
  return std::hash<bool>{}(true);  // 有骨骼 → 固定值
}
```

> 约定：无 Skeleton 时不参与 hash，而非传入"无骨骼"的 hash。有无骨骼由 RenderingItem 中 skeleton 字段是否存在决定。

### R2: RenderingItem 产生 PipelineKey

`RenderingItem` 聚合所有 `IPipelineInfluencer`，提供 `buildPipelineKey()` 方法：

```cpp
struct RenderingItem {
  // 现有字段...
  std::shared_ptr<Mesh>             mesh;
  std::shared_ptr<RenderPassEntry>  passEntry;      // 含 shader + renderState
  std::optional<SkeletonPtr>        skeleton;
  ObjectPCPtr                       objectInfo;

  // 从所有 IPipelineInfluencer 构建 PipelineKey
  PipelineKey buildPipelineKey() const {
    size_t h = 0;
    hash_combine(h, mesh->getPipelineHash());
    hash_combine(h, passEntry->getPipelineHash());
    if (skeleton.has_value()) {
      hash_combine(h, skeleton.value()->getPipelineHash());
    }
    // 将组合 hash 转为规范化字符串注册到 StringID
    std::string keyStr = fmt::format("pk:{:016x}", h);
    return PipelineKey{ StringID(keyStr) };
  }
};
```

### R3: PipelineBuildInfo 从 RenderingItem 产生

Backend 构建 pipeline 所需的全部信息封装在 `PipelineBuildInfo` 中，从 `RenderingItem` 提取：

```cpp
// src/core/resources/pipeline_key.hpp
namespace LX_core {

struct PipelineBuildInfo {
  PipelineKey key;

  // Shader 字节码
  std::vector<ShaderStageCode> stages;

  // 反射信息（descriptor 布局）
  std::vector<ShaderResourceBinding> bindings;

  // 顶点输入
  VertexLayout vertexLayout;

  // 渲染状态
  RenderState renderState;

  // 图元拓扑
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;

  // 从 RenderingItem 构建
  static PipelineBuildInfo fromRenderingItem(const RenderingItem& item);
};

}
```

### R4: 场景管线对象（薄壳）

为支持 pipeline 预构建扫描，引入以下 core 层对象。当前仅定义与 pipeline 缓存相关的方法，核心渲染逻辑后续补充。

#### ImageFormat（core 层格式枚举）

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

Backend 提供映射：`VkFormat toVkFormat(ImageFormat)`.

#### RenderTarget

描述一个渲染输出目标的格式信息（影响 VkRenderPass 兼容性）：

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

#### RenderQueue

一个 pass 内的 drawcall 队列，持有排序后的 RenderingItem 列表：

```cpp
// src/core/scene/render_queue.hpp
namespace LX_core {

class RenderQueue {
public:
  void addItem(RenderingItem item);
  void sort();  // 按 pipelineKey 排序，减少 pipeline 切换

  const std::vector<RenderingItem>& getItems() const;

  /// 收集队列中所有唯一的 PipelineBuildInfo
  std::vector<PipelineBuildInfo> collectUniquePipelineBuildInfos() const;

private:
  std::vector<RenderingItem> m_items;
};

}
```

#### FrameGraph

描述一帧的渲染结构，包含多个 pass。每个 pass 对应一个 RenderQueue + RenderTarget：

```cpp
// src/core/scene/frame_graph.hpp
namespace LX_core {

struct FramePass {
  std::string        name;        // "forward", "shadow", ...
  RenderTarget       target;
  RenderQueue        queue;
};

class FrameGraph {
public:
  void addPass(FramePass pass);

  /// 从场景扫描所有 renderable，填充各 pass 的 RenderQueue
  void buildFromScene(const Scene& scene);

  /// 收集所有 pass 中的唯一 PipelineBuildInfo（去重）
  std::vector<PipelineBuildInfo> collectAllPipelineBuildInfos() const;

  const std::vector<FramePass>& getPasses() const;

private:
  std::vector<FramePass> m_passes;
};

}
```

#### Scene 扩展

Scene 增加扫描接口，供 FrameGraph 使用：

```cpp
class Scene {
public:
  // 现有接口...

  /// 返回场景中所有 renderable（供 FrameGraph 遍历）
  const std::vector<IRenderablePtr>& getRenderables() const;
};
```

### R5: 预构建流程

#### 加载时预构建（主要方式）

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
    │  for each info:
    │    pipelineCache.getOrCreate(info, renderPass)
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

### R6: Backend PipelineCache

```cpp
// src/backend/vulkan/details/pipelines/pipeline_cache.hpp
namespace LX_core::backend {

class PipelineCache {
public:
  explicit PipelineCache(VulkanDevice& device);

  /// 查找已缓存的 pipeline，未命中返回 nullopt
  std::optional<std::reference_wrapper<VulkanPipeline>>
  find(const PipelineKey& key);

  /// 查找或构建（miss 时自动 build 并缓存）
  VulkanPipeline& getOrCreate(
      const PipelineBuildInfo& buildInfo,
      VkRenderPass renderPass
  );

  /// 批量预构建
  void preload(
      const std::vector<PipelineBuildInfo>& buildInfos,
      VkRenderPass renderPass
  );

private:
  VulkanDevice& m_device;
  std::unordered_map<PipelineKey, VulkanPipelinePtr, PipelineKey::Hash> m_cache;
};

}
```

### R7: 通用 Pipeline 构建（废除子类模式）

废除 `VkPipelineBlinnPhong` 等硬编码子类。`VulkanPipeline` 改为通用构建，接受 `PipelineBuildInfo`：

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

### R8: 废弃的旧组件

| 文件 | 原因 |
|------|------|
| `vkp_blinnphong.hpp` | Pipeline 子类，被数据驱动方式替代 |
| `vkp_pipeline_slot.hpp` | `PipelineSlotDetails`/`PipelineSlotId` 被 ShaderResourceBinding 替代 |
| `PipelineSlotId` (render_resource.hpp) | 硬编码槽位枚举，被反射信息替代 |

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
 │      │  每个组合 → RenderingItem { mesh, passEntry, skeleton }               │
 │      │             .buildPipelineKey()                                        │
 │      │               → mesh.getPipelineHash()                                │
 │      │               → passEntry.getPipelineHash()                           │
 │      │               → skeleton?.getPipelineHash()                           │
 │      │               → PipelineKey (StringID)                                │
 │      ▼                                                                        │
 │  FrameGraph::collectAllPipelineBuildInfos()                                  │
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

- **core 层无外部依赖**：所有 core 对象（RenderTarget、ImageFormat 等）仅使用 C++ 标准库，backend 提供到 Vulkan 的映射
- **Dynamic state**：Viewport 和 Scissor 保持 dynamic state，不影响 pipeline 唯一性
- **Push constant**：采用引擎约定（128 字节，vertex+fragment stages），所有 pipeline 共享相同 push constant range，不参与 PipelineKey
- **VkPipelineCache**：可选优化项（使用 Vulkan 原生 pipeline cache 加速重启时构建），当前不做要求
- **RenderTarget 对 Pipeline 的影响**：不同 RenderTarget 可能产生不同的 VkRenderPass（color/depth format 差异），PipelineCache::getOrCreate 需要配合正确的 VkRenderPass，但 RenderTarget 的 hash 暂不纳入 PipelineKey（当前只有一个 forward pass）

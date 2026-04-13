# Frame Graph

> 一帧的渲染结构：若干 `FramePass` 组成，每个 pass 有自己的 `RenderQueue`。`FrameGraph` 的首要用途是 pipeline 预构建——在场景加载时一次性扫描所有 pass × renderable，推导出完整的 `PipelineBuildInfo` 集合，喂给 `PipelineCache::preload()`。
>
> 权威 spec: `openspec/specs/frame-graph/spec.md`

## 核心抽象

### `FramePass` (`src/core/scene/frame_graph.hpp:14`)

```cpp
struct FramePass {
    StringID    name;      // Pass_Forward / Pass_Shadow / Pass_Deferred
    RenderTarget target;   // color format + depth format + sample count
    RenderQueue  queue;
};
```

`name` 是 `StringID`（不是 `std::string`）—— 对齐 `Scene::buildRenderingItem(StringID pass)` 的参数类型。

### `FrameGraph` (`src/core/scene/frame_graph.hpp:22`)

```cpp
class FrameGraph {
public:
    void addPass(FramePass pass);
    void buildFromScene(const Scene &scene);
    std::vector<PipelineBuildInfo> collectAllPipelineBuildInfos() const;
    const std::vector<FramePass> &getPasses() const;
};
```

- `buildFromScene`: 遍历每个 pass × 每个 renderable，产出 `RenderingItem` 推入对应 pass 的 queue
- `collectAllPipelineBuildInfos`: 汇总所有 queue 的 unique `PipelineBuildInfo`，按 `PipelineKey` 全局去重

### `RenderQueue` (`src/core/scene/render_queue.hpp:12`)

```cpp
class RenderQueue {
public:
    void addItem(RenderingItem item);
    void sort();                                 // 按 PipelineKey 排序减少 pipeline 切换
    const std::vector<RenderingItem> &getItems() const;
    std::vector<PipelineBuildInfo> collectUniquePipelineBuildInfos() const;
};
```

### `ImageFormat` + `RenderTarget` (`src/core/gpu/`)

- `ImageFormat` (`image_format.hpp`) — `uint8_t` 枚举：`RGBA8` / `BGRA8` / `R8` / `D32Float` / `D24UnormS8` / `D32FloatS8`
- `RenderTarget` (`render_target.hpp:11`) — `{colorFormat, depthFormat, sampleCount}`，提供 `getHash()`

Core 层**不**引用 `VkFormat`。Backend（`src/backend/vulkan/details/`）提供 `toVkFormat(ImageFormat) → VkFormat` 映射。

## 典型用法

```cpp
#include "core/scene/frame_graph.hpp"
#include "core/scene/scene.hpp"
#include "core/scene/pass.hpp"

using namespace LX_core;

auto scene = Scene::create(renderable);
auto frameGraph = std::make_shared<FrameGraph>();

// 配置 pass（只有 Forward 时）
FramePass forward{
    .name   = Pass_Forward,
    .target = RenderTarget{ImageFormat::BGRA8, ImageFormat::D32Float, 1},
    .queue  = {},
};
frameGraph->addPass(std::move(forward));

// 从 Scene 填充每个 pass 的 queue
frameGraph->buildFromScene(*scene);

// 收集所有去重后的 PipelineBuildInfo
auto buildInfos = frameGraph->collectAllPipelineBuildInfos();

// 喂给 backend 的 preload
vulkanPipelineCache.preload(buildInfos, renderPassHandle);
```

## 调用关系

```
scene load / scene change
  │
  ▼
FrameGraph::buildFromScene(scene)
  │
  │ for each pass in m_passes:
  │   for each renderable in scene.getRenderables():
  │     item = Scene::buildRenderingItemForRenderable(renderable, pass.name)
  │     pass.queue.addItem(std::move(item))
  │
  ▼
FrameGraph::collectAllPipelineBuildInfos()
  │
  │ 对每个 pass：
  │   queue.collectUniquePipelineBuildInfos()   // 本 queue 内按 key 去重
  │ 全局再按 key 去重一次
  │
  ▼
Backend 收到 vector<PipelineBuildInfo>
  │
  ▼
PipelineCache::preload(buildInfos, renderPass)
  │ for each info:
  │   PipelineCache::getOrCreate(info, renderPass)  // 但不报 miss warning
  │
  ▼
所有 pipeline 就绪

─────── 运行期 ───────

for each FramePass in frameGraph:
    beginRenderPass(pass.target)
    queue.sort()  // 减少 pipeline 切换
    for each RenderingItem in queue.getItems():
        pipeline = cache.find(item.pipelineKey)  // O(1)，应命中
        cmd->bindPipeline(pipeline)
        cmd->bindResources(item)
        cmd->draw(item)
```

## 注意事项

- **Scene 接口已扩展**: 老版 `Scene` 只持有单个 `IRenderablePtr mesh`。REQ-003b 改为 `std::vector<IRenderablePtr> m_renderables` + `getRenderables()` 访问器，配合 `buildRenderingItemForRenderable(renderable, pass)` 辅助方法让 `FrameGraph::buildFromScene` 能逐个迭代。
- **`FramePass.name` 必须是 `StringID`**: 不是 `std::string`，避免 frame-graph 和 `Scene::buildRenderingItem(pass)` 用两种不兼容的 key 类型。
- **全局去重发生两次**: `RenderQueue::collectUniquePipelineBuildInfos()` 在 queue 内部去重，`FrameGraph::collectAllPipelineBuildInfos()` 再跨 queue 全局去重一次。即使"同一个 material 在两个 pass 里的 `PipelineKey` 相同"，也只产生一个 `PipelineBuildInfo`。
- **`RenderTarget` 目前不参与 `PipelineKey`**: 只有一个 forward pass 时没必要；预留给未来多目标。等真的需要时要把 target 的 hash 也混进 `compose(TypeTag::PipelineKey, ...)` 或者单独开一级 target-aware key。
- **排序是弱保证**: `RenderQueue::sort()` 按 `PipelineKey` 排序，但对于相同 key 的 item 是 stable 的（按插入序保持相对顺序），避免因排序改变语义相关的 draw order。

## 测试

- `src/test/integration/test_frame_graph.cpp` — 覆盖 single-pass build、多 renderable 的收集、跨 pass 去重
- `src/test/integration/test_pipeline_build_info.cpp` — `fromRenderingItem` 的 backend-agnostic 契约

## 延伸阅读

- `openspec/specs/frame-graph/spec.md` — `FramePass` / `FrameGraph` / `RenderQueue` / `ImageFormat` / `RenderTarget` 的所有 normative 要求
- `openspec/specs/pipeline-build-info/spec.md` — 下游的 `PipelineBuildInfo` 派生逻辑
- `openspec/specs/pipeline-cache/spec.md` — `preload` 契约
- 归档: `openspec/changes/archive/2026-04-13-pipeline-prebuilding/` — REQ-003b 的完整实施

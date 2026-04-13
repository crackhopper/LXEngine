# Pipeline Cache

> Vulkan backend 的 pipeline 存储。独立于 `VulkanResourceManager` —— 后者只做一个薄壳转发。支持 `find(key)` / `getOrCreate(buildInfo, renderPass)` / `preload(...)`，把 pipeline 身份（`PipelineKey`）和 pipeline 构造输入（`PipelineBuildInfo`）严格分离。
>
> 权威 spec: `openspec/specs/pipeline-cache/spec.md`

## 核心抽象

**位置**: `src/backend/vulkan/details/pipelines/`

```cpp
namespace LX_core::backend {

class PipelineCache {
public:
    explicit PipelineCache(VulkanDevice &device);

    std::optional<std::reference_wrapper<VulkanPipeline>>
    find(const PipelineKey &key);

    VulkanPipeline &getOrCreate(const PipelineBuildInfo &buildInfo,
                                VkRenderPass renderPass);

    void preload(const std::vector<PipelineBuildInfo> &buildInfos,
                 VkRenderPass renderPass);

private:
    VulkanDevice &m_device;
    std::unordered_map<PipelineKey, VulkanPipelinePtr, PipelineKey::Hash> m_cache;
};

}
```

## 语义

### `find(key)`

- 命中 → 返回 `optional<ref<VulkanPipeline>>`
- 未命中 → 返回 `nullopt`，**不**构建、**不**分配
- 用于"我期望 pipeline 已经预构建好"的热路径

### `getOrCreate(buildInfo, renderPass)`

- 命中 → 返回缓存的 `VulkanPipeline&`
- 未命中 → 构建新 pipeline，缓存，**发出 warning log**（使用 `toDebugString(key.id)` 标注）
- Warning 是故意的：运行期 miss 表示 preload 遗漏了一个 shader 变体，开发者需要知道

### `preload(buildInfos, renderPass)`

- 遍历输入，对每个 `info` 调 `getOrCreate` 但**抑制 warning**
- 被设计为"首次构建"的主路径
- 幂等：重复 preload 相同输入不会重建

## 典型用法

```cpp
#include "backend/vulkan/details/pipelines/pipeline_cache.hpp"

using namespace LX_core::backend;

PipelineCache cache(device);

// 场景加载时
auto buildInfos = frameGraph->collectAllPipelineBuildInfos();
cache.preload(buildInfos, renderPass.getHandle());
// 所有 pipeline 就绪，无 warning

// 每帧渲染时
for (const auto &item : frameGraph->getPasses()[0].queue.getItems()) {
    auto pipelineRef = cache.find(item.pipelineKey);
    if (!pipelineRef) {
        // fallback: 运行期出现新 material
        auto info = PipelineBuildInfo::fromRenderingItem(item);
        auto &pipeline = cache.getOrCreate(info, renderPass.getHandle());
        // ← 会 emit warning，提示 preload 漏了
        cmdBuffer.bindPipeline(pipeline);
    } else {
        cmdBuffer.bindPipeline(pipelineRef->get());
    }
}
```

## 与 `VulkanResourceManager` 的关系

`VulkanResourceManager` 现在是 `PipelineCache` 的**thin forwarder**:

```cpp
// VulkanResourceManager (简化)
class VulkanResourceManager {
public:
    VulkanPipeline &getOrCreateRenderPipeline(const RenderingItem &item) {
        auto info = PipelineBuildInfo::fromRenderingItem(item);
        return m_pipelineCache.getOrCreate(info, m_renderPass->getHandle());
    }

private:
    PipelineCache m_pipelineCache;  // 不再是 unordered_map<PipelineKey, VulkanPipelinePtr>
    // ...
};
```

**ResourceManager 不再直接持有 pipeline map**。任何 `std::unordered_map<PipelineKey, VulkanPipelinePtr>` 只能出现在 `PipelineCache` 内部。

## 调用关系

```
(预构建期) VulkanRenderer::initScene(scene)
  │
  ├── frameGraph.buildFromScene(scene)
  │
  ├── buildInfos = frameGraph.collectAllPipelineBuildInfos()
  │
  └── pipelineCache.preload(buildInfos, renderPass)
        │
        └── for each info:
              │ getOrCreate(info, rp)（抑制 warning）
              ▼
              VulkanPipeline::build(device, info, renderPass)
              m_cache[info.key] = pipeline

─────── 运行期 ───────

VulkanRenderer::draw(item)
  │
  ├── pipelineCache.find(item.pipelineKey)
  │     │
  │     ├── 命中 → 返回 cached VulkanPipeline
  │     └── miss  → fallback getOrCreate + warning
  │
  ▼
cmdBuffer.bindPipeline(pipeline)
cmdBuffer.bindResources(item, pipeline)
cmdBuffer.draw(item)
```

## 注意事项

- **`find` 绝不构建**: 这是和 `getOrCreate` 的关键区别。`find` 专门用于 hot path 里"我 expect 它已经在那儿"的场景；如果你发现自己每次 `find` 都 fallback 到 `getOrCreate`，那是 preload 的 bug，不是 `find` 的 bug。
- **Warning 是可观测性契约**: `getOrCreate` 在 miss 时**必须** emit warning，这是为了让开发者能从日志里看到 preload 漏了哪些 key。Test 里可以抓这个 warning 来验证 preload 完整性。
- **幂等 preload**: 如果未来加入热重载或多场景切换，preload 可能被调用多次，实现必须保证不重建已存在的条目。
- **`PipelineKey::Hash` 来自 core**: `unordered_map` 的 hasher 用 `PipelineKey::Hash`，实际只是对 `StringID::id` 取整数 hash —— 零碰撞。
- **不属于 core**: 这个类是 backend 特有的，位于 `LX_core::backend` namespace（`src/backend/vulkan/details/pipelines/`），不能跨到其他 backend 实现复用。当有第二个 backend 时再提取公共基类。

## 测试

- `src/test/integration/test_pipeline_cache.cpp`（REQ-003b 新增）— find cold miss → preload → find hit；getOrCreate warning；preload 幂等

## 延伸阅读

- `openspec/specs/pipeline-cache/spec.md` — `find` / `getOrCreate` / `preload` 的完整契约
- `openspec/specs/renderer-backend-vulkan/spec.md` — ResourceManager 作为 thin forwarder 的约定
- `notes/subsystems/frame-graph.md` — 上游 preload 输入的产出方
- `notes/subsystems/pipeline-identity.md` — `PipelineKey` 和 `PipelineBuildInfo` 的定义
- 归档: `openspec/changes/archive/2026-04-13-pipeline-prebuilding/` — REQ-003b 的完整实施

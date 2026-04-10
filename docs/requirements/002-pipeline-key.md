# REQ-002: PipelineKey 设计

## 背景

当前 backend 通过硬编码字符串（`"blinnphong_0"`）查找已构建的 Vulkan Pipeline，无法根据 RenderingItem 的实际属性自动匹配。随着 Shader 变体、多种顶点格式、不同渲染状态的引入，需要一种自动化的 Pipeline 标识机制。

## 目标

设计 `PipelineKey`，使 `RenderingItem` 能自动产生唯一标识，backend 据此缓存和查找已构建的 Pipeline。

## 设计决策

### 采用 StringID 方案

将所有影响 Pipeline 唯一性的因素拼接为规范化字符串，注册到 `GlobalStringTable` 得到 `StringID`。

**理由**：
- 零碰撞（字符串完全匹配才相等）
- 比较 O(1)（uint32_t 比较）
- 调试友好（`getName(id)` 可打印完整 key 内容）
- 复用已有 `GlobalStringTable` 基础设施
- 与 backend 现有 `string` key 自然过渡

### PipelineKey 定义在 core 层

PipelineKey 是 core 层的 **内部概念**，用户无需感知。所有构成因素（vertex layout、shader、render state、topology）均为 core 已有概念，不涉及 backend API 细节。

## 需求

### R1: PipelineKey 定义

```
src/core/resources/pipeline_key.hpp
```

```cpp
namespace LX_core {

struct PipelineKey {
  StringID id;    // 唯一标识

  bool operator==(const PipelineKey& rhs) const { return id == rhs.id; }
  bool operator!=(const PipelineKey& rhs) const { return id != rhs.id; }

  // 用于 unordered_map
  struct Hash {
    size_t operator()(const PipelineKey& k) const {
      return StringID::Hash{}(k.id);
    }
  };
};

}
```

### R2: PipelineKey 的构成因素

以下因素共同决定 Pipeline 的唯一性：

| 序号 | 因素 | 来源（getPipelineHash） | 格式示例 |
|------|------|------------------------|----------|
| 1 | Shader 名称 + 变体 | `ShaderProgramSet.getPipelineHash()` | `pbr\|HAS_NORMAL_MAP` |
| 2 | 顶点布局 + 拓扑 | `Mesh.getPipelineHash()` | `vl:0x3a2f1b7c\|tri` |
| 3 | 渲染状态 | `RenderState.getPipelineHash()` | `rs:0x7c1de4a0` |
| 4 | 骨骼动画标志 | `Skeleton.getPipelineHash()` | `+skel` 或空 |

各资源统一通过 `getPipelineHash()` 方法提供其对 PipelineKey 的贡献（见 REQ-001 R6）。

### R3: 规范化字符串格式

各因素之间用 `|` 分隔，变体宏按字典序排序后用 `,` 分隔：

```
{shaderName}|{variant1,variant2,...}|vl:{hash}|rs:{hash}|{topology}[|+skel]
```

示例：

```
pbr||vl:0x3a2f1b7c|rs:0x7c1de4a0|tri              // 基础 PBR，无变体，无骨骼
pbr|HAS_NORMAL_MAP|vl:0x3a2f1b7c|rs:0x7c1de4a0|tri  // + 法线贴图
pbr|HAS_NORMAL_MAP|vl:0x8b1c2e3d|rs:0x7c1de4a0|tri|+skel  // + 骨骼动画
```

### R4: PipelineKey 构建函数

提供静态工厂方法，从各组件数据构建 key：

```cpp
struct PipelineKey {
  static PipelineKey build(
    const ShaderProgramSet& shaderSet,
    const VertexLayout& vertexLayout,
    const RenderState& renderState,
    PrimitiveTopology topology,
    bool hasSkeleton
  );
};
```

此函数负责：
1. 收集所有因素
2. 按规范格式拼接字符串
3. 通过 `StringID` 构造函数注册到 `GlobalStringTable`
4. 返回 `PipelineKey`

### R5: RenderingItem 产生 PipelineKey

`RenderingItem` 新增 `pipelineKey` 字段，在 `buildRenderingItem()` 时由 `Scene` 自动填充：

```cpp
struct RenderingItem {
  // 现有字段...
  PipelineKey pipelineKey;    // 新增
};
```

`Scene::buildRenderingItem()` 中调用 `PipelineKey::build(...)` 从 RenderingItem 的各属性生成 key。

### R6: Backend 使用 PipelineKey

Backend 将 `m_pipelines` 的 key 从 `std::string` 改为 `PipelineKey`（或直接使用 `StringID`）：

```cpp
// Before:
std::unordered_map<std::string, VulkanPipelinePtr> m_pipelines;

// After:
std::unordered_map<PipelineKey, VulkanPipelinePtr, PipelineKey::Hash> m_pipelines;
```

收到 `RenderingItem` 后，直接用 `item.pipelineKey` 查找：
- 命中 → 使用已缓存的 Pipeline
- 未命中 → 创建新 Pipeline 并缓存

## 数据流

```
Scene::buildRenderingItem()
│
├── 从 IRenderable 收集: shaderSet, vertexLayout, renderState, topology
├── 判断: hasSkeleton
│
├── PipelineKey::build(shaderSet, vertexLayout, renderState, topology, hasSkeleton)
│   │
│   ├── 拼接规范化字符串: "pbr|HAS_NORMAL_MAP|vl:0x..|rs:0x..|tri|+skel"
│   └── StringID(规范化字符串) → uint32_t
│
└── RenderingItem { ..., pipelineKey }
        │
        ▼
    Backend::draw(item)
        │
        ├── m_pipelines.find(item.pipelineKey)
        ├── hit?  → bind pipeline
        └── miss? → createPipeline(...), cache, bind
```

## 边界与约束

- PipelineKey 是 core 内部概念，不暴露给用户（用户操作的是 MaterialTemplate、Mesh 等上层对象）
- 规范化字符串的构造开销只在首次创建时发生，后续查找全是 `uint32_t` 比较
- 如果未来需要新增影响 Pipeline 的因素（如 MSAA sample count），只需扩展 `build()` 函数和字符串格式

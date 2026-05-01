# REQ-001: Skeleton 迁移至 resources 层

> **Superseded by REQ-007（R6/R7 部分）**：本文档 R6「统一 `getPipelineHash()` 命名规范」与 R7「`PipelineKey::build()` 签名」由 REQ-007「结构化 Interning 驱动的 Pipeline Identity」替换——`getPipelineHash()` 让位于 `getRenderSignature(pass)`，`PipelineKey::build()` 改为两级 compose `(objectSig, materialSig)`。R1–R5（Skeleton 迁移本身）仍然有效。归档保留历史上下文；当前实现以 REQ-007 为准。

## 背景

Mesh、Material 已完成重构，统一放在 `src/core/resources/` 下。Skeleton 目前仍在 `src/core/scene/components/` 中，继承自 `IComponent`。`IComponent` 仅有一个 `getRenderResources()` 方法，且只有 `Skeleton` 一个实现者，抽象层过薄无存在价值。

## 目标

将 Skeleton 从 `scene/components/` 迁移至 `resources/`，去除 `IComponent` 抽象，使其与 Mesh 平级成为一个 **资源管理器**（管理骨骼数据 + UBO）。

## 需求

### R1: 文件迁移

- `Skeleton` 类和 `SkeletonUBO` 从 `src/core/scene/components/skeleton.hpp` 迁移至 `src/core/resources/skeleton.hpp`
- `Bone` 结构体随之迁移
- 删除 `src/core/scene/components/skeleton.hpp` 和 `skeleton.cpp`

### R2: 去除 IComponent

- `Skeleton` 不再继承 `IComponent`
- 删除 `src/core/scene/components/base.hpp`（`IComponent` 定义）
- 删除 `src/core/scene/components/` 目录（如无其他文件）

### R3: Skeleton 公开接口

迁移后 Skeleton 的公开接口：

```cpp
class Skeleton {
public:
  static SkeletonPtr create(const vector<Bone>& bones, ResourcePassFlag passFlag);
  bool addBone(const Bone& bone);
  void updateUBO();
  SkeletonUboPtr getUBO() const;            // 替代原 getRenderResources()
  size_t getPipelineHash() const;            // 见 R5
};
```

- `getRenderResources()` 替换为更明确的 `getUBO()` 直接返回 `SkeletonUboPtr`
- **不**提供 `hasSkeleton()`：Skeleton 实例存在即意味着"有骨骼"，该方法恒为 true 没有判断意义。"有/无骨骼"的真正布尔位由调用方持有（例如 `RenderableSubMesh::skeleton` 是否为空）

### R4: 更新引用方

以下文件需要更新 include 路径和使用方式：

- `src/core/scene/object.hpp` — `RenderableSubMesh` 中引用 Skeleton
- `src/core/resources/mesh.hpp` — 删除对 `components/base.hpp` 的 include

### R5: Skeleton 提供 getPipelineHash()

骨骼动画影响 pipeline 的方式为 **布尔标志**（有/无骨骼），体现在：

- 顶点格式不同：`VertexSkinned`（含 boneIds + weights）vs 其他格式
- 额外的 SkeletonUBO 绑定
- Push constant 中 `enableSkinning` 标志

Skeleton 须提供 `getPipelineHash()` 方法，返回一个固定的非零 tag（例如 `kSkeletonPipelineHashTag`），代表"启用骨骼"这一维度。

```cpp
class Skeleton {
public:
  // ...
  size_t getPipelineHash() const;  // 返回固定 tag
};
```

PipelineKey 的消费方式（与 R6 协同）：

- `PipelineKey::build()` 接收 `const SkeletonPtr&`（可空）
- 内部计算：`skeleton ? skeleton->getPipelineHash() : 0`
- 0 即代表"无骨骼"，非 0 代表"启用骨骼"

这样不需要额外的布尔参数，所有"是否参与 pipeline 标识"的信息都统一通过 `getPipelineHash()` 通道流入。

### R6: 统一 getPipelineHash() 命名规范

所有贡献 PipelineKey 构建因子的资源，须统一提供 `getPipelineHash()` 方法。以下现有资源需要改造：

| 资源 | 当前方法 | 改造 |
|------|---------|------|
| `Mesh` | `getLayoutHash()` | 新增 `getPipelineHash()`（含顶点布局 + 拓扑） |
| `RenderState` | `getHash()` | 新增 `getPipelineHash()`（别名） |
| `ShaderProgramSet` | `getHash()` | 新增 `getPipelineHash()`（别名） |
| `Skeleton` | 无 | 新增 `getPipelineHash()` |

规范要求：

- 每个参与 PipelineKey 的资源类都有一个 `getPipelineHash() -> size_t`
- 原有 `getHash()` / `getLayoutHash()` 保留不删（内部仍可用），`getPipelineHash()` 可委托给它们
- 阅读代码时，看到 `getPipelineHash()` 即知"此值参与 pipeline 标识"
- `PipelineKey::build()` 内部调用各资源的 `getPipelineHash()` 来组装 key
- **Mesh 的 `getPipelineHash()` 须包含拓扑**：`PrimitiveTopology` 由 `Mesh`（实际上是 `IndexBuffer`）持有，按"谁拥有谁贡献"原则混入 Mesh 自己的 hash，不再作为 `PipelineKey::build()` 的独立参数

### R7: PipelineKey::build() 签名

为兑现 R6 的"统一通道"，`PipelineKey::build()` 接受**资源对象**而不是零散字段：

```cpp
static PipelineKey build(const ShaderProgramSet& shaderSet,
                         const Mesh& mesh,
                         const RenderState& renderState,
                         const SkeletonPtr& skeleton);   // 可空
```

实现内部对每个资源调用 `getPipelineHash()`（skeleton 为空时贡献 0），不再有 `PrimitiveTopology topology` 和 `bool hasSkeleton` 两个零散参数。

> 注：此签名与 REQ-002 中描述的旧签名 `(shaderSet, vertexLayout, renderState, topology, hasSkeleton)` 不同，REQ-002 文档需要在落地时同步更新。

## 实施状态（2026-04-13）

- R1/R2/R4 — 已完成（Skeleton 在 `src/core/resources/`，`scene/components/` 目录已删除）
- R3 — 已完成（含本次 `hasSkeleton()` 移除）
- R5/R6/R7 — 本次需求执行后完成

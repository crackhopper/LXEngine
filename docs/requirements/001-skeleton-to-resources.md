# REQ-001: Skeleton 迁移至 resources 层

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

### R3: Skeleton 接口保持

迁移后 Skeleton 的公开接口不变：

```cpp
class Skeleton {
public:
  static SkeletonPtr create(const vector<Bone>& bones, ResourcePassFlag passFlag);
  bool addBone(const Bone& bone);
  void updateUBO();
  SkeletonUboPtr getUBO() const;            // 替代原 getRenderResources()
  bool hasSkeleton() const;                  // 新增：用于 PipelineKey 判断
};
```

- `getRenderResources()` 替换为更明确的 `getUBO()` 直接返回 `SkeletonUboPtr`
- 新增 `hasSkeleton()` 返回 `true`（为后续 PipelineKey 布尔判断提供语义接口）

### R4: 更新引用方

以下文件需要更新 include 路径和使用方式：

- `src/core/scene/object.hpp` — `RenderableSubMesh` 中引用 Skeleton
- `src/core/resources/mesh.hpp` — 删除对 `components/base.hpp` 的 include

### R5: Skeleton 提供 getPipelineHash()

骨骼动画影响 pipeline 的方式为 **布尔标志**（有/无骨骼），体现在：

- 顶点格式不同：`VertexSkinned`（含 boneIds + weights）vs 其他格式
- 额外的 SkeletonUBO 绑定
- Push constant 中 `enableSkinning` 标志

Skeleton 须提供 `getPipelineHash()` 方法，返回参与 PipelineKey 构建的 hash 值：

```cpp
class Skeleton {
public:
  // ...
  size_t getPipelineHash() const;  // 基于 hasSkeleton 布尔值
};
```

此布尔信息将由 PipelineKey 消费（见 REQ-002）。

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

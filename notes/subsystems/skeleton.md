# Skeleton

> Skeleton 是独立资源，不再挂在旧组件体系上。它既提供骨骼矩阵 UBO，也提供“是否启用骨骼”的 pipeline identity 信号。
>
> 权威 spec: `openspec/specs/skeleton-resource/spec.md`

## 它解决什么问题

- 把骨骼数据变成可同步到 GPU 的资源。
- 让 skinned 和 non-skinned draw 拥有不同的 pipeline 身份。
- 给 renderable 提供可选的骨骼 descriptor 资源。

## 核心对象

- `Bone`：单根骨骼的数据。
- `SkeletonUBO`：GPU 侧骨骼矩阵数组。
- `Skeleton`：骨骼资源管理入口。

## 典型数据流

1. 创建 `Skeleton`。
2. `RenderableSubMesh` 可选持有它。
3. `getDescriptorResources()` 把 `SkeletonUBO` 加进来。
4. `getRenderSignature(...)` 用 `Skn1` 表示“启用骨骼”。
5. backend 按 `Bones` 这个 binding 名完成 descriptor 路由。

## 关键约束

- `MAX_BONE_COUNT` 目前固定为 128。
- shader 里的 block 名必须叫 `Bones`。
- “无骨骼”由调用方返回空 `StringID` 表达，不是 `Skeleton` 自己返回 0。
- 某些 shader layout 要求即使无骨骼也绑定一个空 skeleton。

## 从哪里改

- 想改骨骼上限：同时改 shader 和 `SkeletonUBO`。
- 想改 skinned pipeline identity：看 `getRenderSignature()` 路径。
- 想改 descriptor 绑定：看 `getBindingName()` 和 shader block 名。

## 关联文档

- `openspec/specs/skeleton-resource/spec.md`
- `notes/subsystems/scene.md`
- `notes/subsystems/pipeline-identity.md`

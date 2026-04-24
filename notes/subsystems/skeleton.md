# Skeleton

> Skeleton 是独立资源，不再挂在旧组件体系上。它现在只负责骨骼矩阵数据和运行期 descriptor 供给，不再直接参与 pipeline identity。
>
> 权威 spec: `openspec/specs/skeleton-resource/spec.md`

## 它解决什么问题

- 把骨骼数据变成可同步到 GPU 的资源。
- 给 skinned pass 提供 `Bones` UBO。
- 作为 `SceneNode` 结构性校验里的运行期依赖。

## 核心对象

- `Bone`：单根骨骼的数据。
- `SkeletonData`：GPU 侧骨骼矩阵数组。
- `Skeleton`：骨骼资源管理入口。

## 典型数据流

1. 创建 `Skeleton`。
2. `SceneNode` 可选持有它。
3. 结构校验时，如果某个 enabled pass 打开了 `USE_SKINNING`，`SceneNode` 会要求 skeleton 存在且 shader 确实声明了 `Bones`。
4. draw 装配时，skeleton 的 UBO 进入 descriptor resources。
5. backend 按 `Bones` 这个 binding 名完成 descriptor 路由。

## 关键约束

- `MAX_BONE_COUNT` 目前固定为 128。
- shader 里的 block 名必须叫 `Bones`。
- `Skeleton` 不再暴露 `getPipelineSignature()` 或 `getPipelineHash()`。
- 是否切 pipeline 不看 skeleton 对象本身，而看 material pass 的 shader variants。

## 当前实现边界

- 如果某个 pass 开启 skinning variant 但节点没有 skeleton，系统会在 `SceneNode` 校验阶段直接 `FATAL + terminate`。
- 如果 shader variant 和 `Bones` binding 声明不一致，同样会在 `SceneNode` 校验阶段失败。
- skeleton 仍是纯 runtime 资源提供者，不承担 scene graph 组件职责。

## 从哪里改

- 想改骨骼上限：同时改 shader 和 `SkeletonData`。
- 想改 skinned pipeline 身份：看 material pass variants，不要改 `Skeleton`。
- 想改 descriptor 绑定：看 `getBindingName()` 和 shader block 名。

## 关联文档

- `notes/subsystems/scene.md`
- `notes/subsystems/material-system.md`
- `notes/subsystems/pipeline-identity.md`

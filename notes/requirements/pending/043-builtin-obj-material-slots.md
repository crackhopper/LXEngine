# REQ-043: 内置 OBJ 资产的材质槽与 MTL 颜色支持

> 2026-05-17：本需求已从 active 队列移入 pending。它仍是资产质量修补项，但不是 v0.1.1 FrameGraph / Shadow / CSM 主线的前置。

## 背景

第一批内置模型资产已经保留 OBJ、MTL 和贴图文件。当前编辑器能把带 `map_Kd`
的资产转成单一 `albedoMap` 绑定，并用 `blinnphong_textured.material` 渲染。

但 Kenney 的部分低面元 OBJ 没有贴图，只有 MTL 中的多个 `newmtl` / `Kd`
纯色材质；另一些 OBJ 虽然有贴图，也仍然保留 `usemtl` 的面级材质分配。当前
`SceneNode` 只有一个 `MeshComponent` + 一个 `MaterialComponent`，OBJ loader
也只输出一个合并 mesh，因此不能忠实表达“同一个 OBJ 内多个材质槽”的内容。

## 目标

1. 让内置 OBJ 能按 MTL 材质槽渲染纯色材质和贴图材质。
2. 让没有贴图的低面元模型仍按原始 `Kd` 颜色显示，而不是统一灰色。
3. 保持当前内置资产目录继续保留原始 OBJ、MTL、贴图和 credits。

## 需求

### R1: OBJ loader 暴露材质槽

OBJ loader 需要在读取 `usemtl` / `mtllib` 后输出材质槽信息：

| 字段 | 含义 |
|---|---|
| `materialName` | OBJ `usemtl` 名称 |
| `diffuseColor` | MTL `Kd` 颜色 |
| `albedoTexture` | MTL `map_Kd` 路径，可为空 |
| `indexRange` | 当前材质覆盖的 index 范围 |

### R2: Runtime 支持一个模型多个 draw item

内置模型 runtime 需要能把一个 OBJ 拆成多个可渲染段。实现可以选择：

- 一个 `SceneNode` 下挂多个 child renderable；
- 或者扩展 mesh/material component，支持 submesh + material slot。

无论采用哪种方案，scene tree 中仍应把用户拖入的资产视为一个可操作对象。

### R3: 纯色 MTL 映射到材质参数

没有 `map_Kd` 的材质槽应使用 `blinnphong_lit.material`，并把 `Kd` 写入
`MaterialUBO.baseColor`。

### R4: 贴图 MTL 映射到 albedoMap

有 `map_Kd` 的材质槽应使用 `blinnphong_textured.material`，绑定对应贴图到
`albedoMap`，并启用 `MaterialUBO.enableAlbedo`。

### R5: 保存与重载保持资产语义

场景保存时不应把每个材质槽展开成不可识别的临时节点。重载后仍应能从内置
asset manifest 或 scene document 中恢复材质槽。

### R6: 测试覆盖

覆盖：

- 单贴图 OBJ 正确绑定 `albedoMap`。
- 多个 `Kd` 材质槽的 OBJ 渲染段数量与 MTL 一致。
- 删除、复制、保存、重载后资产仍作为一个用户对象操作。
- 缺失 MTL 或贴图时给出可诊断错误或明确 fallback。

## 修改范围

- `src/infra/mesh_loader/obj_mesh_loader.*`
- `src/core/asset/mesh.*`
- `src/core/scene/components/mesh_component.*`
- `src/core/scene/components/material_component.*`
- `src/demos/lxe_editor/scene_builder.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/demos/lxe_editor/builtin_asset_catalog.*`
- 相关 tests

## 边界与约束

- 本需求不引入完整 PBR。
- 本需求不要求支持 OBJ 的所有 MTL 扩展字段。
- 本需求不要求导入不可商用资产。
- 本需求不改变当前已能读取的单贴图 `albedoMap` 路径。

## 实施状态

Pending，未开始。当前已完成的是内置资产保留 MTL/贴图文件，并对单贴图资产做
`albedoMap` 绑定；多材质槽和纯 `Kd` 颜色仍需要本需求补齐。

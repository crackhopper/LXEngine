# REQ-060-a: 3DGS Asset Budget And PLY Sample

> 2026-05-28 新增：引入一个真实 3DGS PLY 样例，并把资产预算从“小型 mesh 测试资产”扩展到“包含一个代表性 3DGS 源 PLY”。

## 背景

3D Gaussian Splatting 的 `.ply` 与普通 mesh PLY 不同。它没有三角面拓扑，而是把每个 vertex 当成一个 Gaussian splat，存储中心点、各向异性尺度、旋转、opacity 和球谐颜色系数。真实 3DGS 源 PLY 通常是几十 MB 到数 GB，仓库原先的 100 MB 总资产预算无法承载一个有代表性的样例。

本需求只处理资产和约定，不实现渲染代码。

## 目标

1. 提交一个可离线使用的 3DGS PLY 样例。
2. 明确该样例的来源、许可、大小、hash 和 PLY 字段。
3. 更新资产预算，让后续实现不再与当前资产 spec 冲突。
4. 明确仓库提交体积控制在 800 MB 以内。
5. 提交一个可作为后续验收入口的 scene preset。

## 需求

### R1: 3DGS PLY 样例入库

仓库 SHALL 包含：

- `assets/models/3dgs_train_sample/point_cloud.ply`
- `assets/models/3dgs_train_sample/README.md`

README SHALL 记录来源、许可、原始路径、文件大小、SHA-256、vertex count 和核心字段。

### R2: 资产预算调整

`openspec/specs/asset-directory-convention/spec.md` SHALL 允许一个 3DGS 代表性源 PLY，使 `assets/` 总体预算从 100 MB 调整到 300 MB。

### R3: 仓库提交体积预算

`openspec/specs/asset-directory-convention/spec.md` SHALL 明确：正常 checkout 内容加 `.git` 对象存储应控制在 800 MB 以内；本地忽略的 `build*`、`.site`、`.cache`、`.worktrees` 不计入提交预算。

### R4: 场景预置

仓库 SHALL 包含：

- `assets/scenes/3dgs_train_sample.scene.yaml`

该 scene SHALL 引用 `assets/models/3dgs_train_sample/point_cloud.ply`，并提供 camera / editor camera。当前运行时尚未渲染 `.ply` 时，该场景可先作为文档化验收入口；后续 REQ 实现后同一文件变成可视化验收场景。

### R5: 资产总览更新

`assets/README.md` SHALL 列出 3DGS 样例，并把总大小更新到当前事实。

## 修改范围

- `assets/models/3dgs_train_sample/`
- `assets/scenes/3dgs_train_sample.scene.yaml`
- `assets/README.md`
- `openspec/specs/asset-directory-convention/spec.md`

## 依赖

- `openspec/specs/asset-directory-convention/spec.md`

## 实施状态

Draft，资产已落地，等待后续渲染能力消费。

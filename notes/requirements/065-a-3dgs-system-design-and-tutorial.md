# REQ-065-a: 3DGS System Design And Tutorial

> 2026-05-28 新增：在功能闭环后补齐 3DGS PLY 渲染的系统设计说明、原理讲解和入门路径。

## 背景

3DGS 横跨文件格式、资源加载、GPU buffer、透明合成、排序、camera projection 和 editor 验收。没有文档时，后续开发者容易把它误解为“点云”或“mesh PLY”。

## 目标

1. 解释 3DGS PLY 与 mesh PLY 的区别。
2. 说明 LXEngine 中 loader、resource、scene node、Vulkan pass 的边界。
3. 讲清底层原理：Gaussian 参数、SH 颜色、投影 footprint、alpha compositing、排序。
4. 给出参考资料和后续优化路线。

## 需求

### R1: 系统设计文档

新增或更新：

- `notes/concepts-design/rendering-pipeline/3dgs-ply-rendering.md`

文档 SHALL 使用“我们”视角，明确哪些是当前已实现能力，哪些是 active REQ 规划。

### R2: 入门教程

功能落地后 SHALL 新增教程：

- `notes/tutorial/3dgs-ply-scene.md`

教程 SHALL 说明如何构建、打开 `3dgs_train_sample.scene.yaml`、观察节点、截图验证、排查黑屏或透明合成问题。

### R3: 参考资料

文档 SHALL 链接至少：

- 3D Gaussian Splatting 原论文 / GraphDeco reference implementation
- 3DGS PLY 字段说明
- Kaolin 3DGS PLY import/export 说明
- Open3D 3DGS rendering design
- gsplat rasterization API
- antimatter15/splat WebGL viewer overview
- 本仓库样例资产 README

### R4: 文档验收

验收 SHALL 覆盖：

- notes build 通过。
- nav 中能访问系统设计文档和教程。
- 文档不声称未实现的优化已经存在。

## 修改范围

- `notes/concepts-design/rendering-pipeline/`
- `notes/tutorial/`
- `notes/nav.yml`

## 依赖

- `REQ-064-a`
- `openspec/specs/notes-writing-style/spec.md`

## 实施状态

Draft，未实施。

# 3DGS PLY 渲染技术调研

> 状态：**已立项为 REQ-060-a..065-a**
> 最后更新：2026-05-28
> 目的：记录 3D Gaussian Splatting PLY 支持的格式、渲染路径和 LXEngine 分阶段落地方案。

## 这个目录是什么

3DGS PLY 支持横跨资产、loader、scene、GPU resource、Vulkan pass、editor 验收和教程。这里保存调研结论，active 需求只保留可实施条目。

## 建议阅读顺序

| # | 文档 | 讲什么 |
|---|---|---|
| 01 | [格式与资产](01-格式与资产.md) | 3DGS PLY 字段、样例资产、为什么不能当 mesh |
| 02 | [渲染算法](02-渲染算法.md) | Gaussian 参数、投影 footprint、排序、alpha 合成 |
| 03 | [LX 当前状态对照](03-LX当前状态对照.md) | 当前 scene / loader / Vulkan 路径能复用什么、缺什么 |
| 04 | [演进路径](04-演进路径.md) | REQ-060-a..065-a 的拆分理由和验收路径 |

## TL;DR

| 结论 | 影响 |
|---|---|
| 3DGS PLY 不是 mesh PLY | 不走 `Mesh` / `IndexBuffer`，新增 `GaussianSplatCloud` |
| PLY 是源格式，通常很大 | 资产预算提升到 300 MB，仓库提交体积目标控制在 800 MB 内 |
| 首版应先显示 `f_dc` 颜色 | 高阶 SH 保留在资源里，后续再做视角相关颜色 |
| 通用 compute pipeline 是前置 | 先补 `REQ-063-a`，再做 3DGS Vulkan splat pass |
| 透明合成需要排序 | 首版可 CPU view-depth sort，后续再 GPU tile/radix |
| Vulkan pass 应与 mesh pass 并存 | 3DGS 作为独立 transparent splat pass 接入 render loop |

## 参考资料

- [Kerbl et al. 2023, 3D Gaussian Splatting for Real-Time Radiance Field Rendering](https://arxiv.org/abs/2308.04079)
- [GraphDeco-INRIA gaussian-splatting](https://github.com/graphdeco-inria/gaussian-splatting)
- [PlayCanvas: The PLY Format](https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/ply/)
- [NVIDIA Kaolin `kaolin.io.ply`](https://kaolin.readthedocs.io/en/latest/modules/kaolin.io.ply.html)
- [Open3D 3D Gaussian Splatting Rendering Design](https://www.open3d.org/docs/latest/cpp_api/md__root__open3_d_cpp_open3d_visualization_rendering_gaussian_splat__gaussian_splat_design.html)
- [gsplat rasterization API](https://docs.gsplat.studio/main/apis/rasterization.html)
- [antimatter15/splat WebGL renderer overview](https://deepwiki.com/antimatter15/splat)
- [Voxel51 Gaussian Splatting dataset](https://huggingface.co/datasets/Voxel51/gaussian_splatting)

# 3DGS PLY 渲染：从高斯云到屏幕椭圆

3D Gaussian Splatting 可以理解成一盒彩色半透明的空间印章：每个印章不是三角面，而是一个带方向、尺寸、颜色和透明度的三维高斯。渲染时我们把这些印章投影到屏幕上，变成许多椭圆形 splat，再按透明度从后到前合成。

这页记录 LXEngine 支持 3DGS PLY 的目标设计。当前仓库已经包含 Voxel51 3DGS 的 assets-downloader catalog 和 cache manifest 表面，但还没有 in-git 大型 PLY、3DGS scene、loader、runtime component 或 Vulkan pass；渲染代码由 `REQ-077-a` 到 `REQ-077-d` 后置分步落地。

## PLY 在这里不是 mesh

普通 PLY 像三角网格的零件表：顶点、法线、UV 和 face index 共同组成表面。3DGS PLY 更像一盒彩色印章的参数表：每一行都是一个独立 Gaussian，没有 face，也不进入 `Mesh` / `IndexBuffer` 路径。

| 数据形态 | 普通 mesh PLY | 3DGS PLY |
|---|---|---|
| 基本单元 | vertex + face | Gaussian splat |
| 拓扑 | face index 定义表面 | 没有连接关系 |
| 颜色 | 常见 RGB / material | `f_dc_*` 和 `f_rest_*` 球谐系数 |
| 形状 | triangle | `scale_*` + `rot_*` 定义各向异性椭球 |
| 透明 | material alpha | 每个 splat 的 `opacity` |
| LXEngine 目标路径 | `Mesh` | `GaussianSplatCloud` |

这也是为什么 3DGS 不应该被塞进现有 `ObjLoader` / `GLTFLoader` 抽象。它需要自己的 loader、资源类型和 render pass。

## 文件字段怎样变成运行时数据

大型样例资产来自 Voxel51 的 Gaussian Splatting Hugging Face 数据集，使用 Apache-2.0 许可，包含 741,883 个 Gaussian splat。当前仓库不提交这个 183 MB PLY，而是通过 assets-downloader 写入 cache：

```text
cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/converted/point_cloud.ply
```

```text
property float x/y/z          # -> GaussianSplat.position
property float f_dc_0..2      # -> GaussianSplat.fDc，0 阶 SH / base color
property float f_rest_0..44   # -> GaussianSplat.fRest，高阶 SH
property float opacity        # -> GaussianSplat.opacity，raw logit
property float scale_0..2     # -> GaussianSplat.scale，raw log-scale
property float rot_0..3       # -> GaussianSplat.rotation，raw quaternion
```

| PLY 字段 | 运行时含义 | 首版消费方式 |
|---|---|---|
| `x/y/z` | 高斯中心点 | 用于 bounds、排序和投影 |
| `f_dc_0..2` | 基础 RGB 球谐项 | 首版直接作为颜色来源 |
| `f_rest_0..44` | 视角相关颜色项 | 首版保留，后续启用 SH shading |
| `opacity` | 透明度 logit | shader 中转成 alpha |
| `scale_0..2` | 三轴 log-scale | shader 中转成椭球半径 |
| `rot_0..3` | 椭球朝向 | shader 中参与协方差投影 |
| `nx/ny/nz` | 来源文件保留字段 | 不作为 mesh normal 使用 |

## 设计边界

3DGS 管线应分成四段，每段只做自己的事。

| 层 | 目标对象 | 职责 |
|---|---|---|
| `infra` | `GaussianSplatPlyLoader` | 解析 PLY header 和 binary vertex records |
| `core` | `GaussianSplatCloud` | 保存 CPU splat 数据、bounds、统计信息 |
| `scene` | `GaussianSplatComponent` | 把资源挂到 scene node，并暴露 editor 摘要 |
| `backend` | Vulkan splat pass | 上传 buffer、排序、投影和 alpha 合成 |

这个分层让 loader 不依赖 Vulkan，也让 Vulkan pass 不需要理解 YAML。Scene 只保存 URI 和 transform，真正的数据由资源加载层构建。

## 渲染路径

首版目标是稳定显示样例场景，而不是一次实现所有论文级优化。通用 compute shader / compute pipeline 支持已经由 finished `REQ-063-a` 落地；3DGS 专用渲染路径在 `REQ-077-c` 消费它。

```text
3DGS PLY
  -> GaussianSplatPlyLoader
  -> GaussianSplatCloud CPU resource
  -> GaussianSplatComponent on SceneNode
  -> GPU buffers
  -> view-depth sort
  -> Vulkan splat draw pass
  -> alpha blended HDR scene color
  -> existing post / ImGui overlay
```

| 阶段 | 要解决的问题 | 首版策略 |
|---|---|---|
| 上传 | 每个 splat 有几十个 float | 先用明确的 GPU buffer layout |
| 排序 | 透明合成依赖顺序 | 可先 CPU 按 view depth 排序 |
| 投影 | 3D 椭球变成屏幕椭圆 | shader 从 scale + rotation 推导 footprint |
| 着色 | 颜色随视角变化 | 先用 `f_dc`，保留 `f_rest` |
| 合成 | 多个透明 splat 叠加 | back-to-front alpha blending |

后续优化可以再引入 GPU tile binning、compute sorting、压缩格式或 SH 高阶着色，但这些不应该阻塞首个可见闭环。

## 场景预置

目标 canonical scene 表面是：

```yaml
gaussianSplat:
  uri: cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/converted/point_cloud.ply
```

当前 scene 文档表面仍以 `mesh.uri` 为主，尚未实现 `gaussianSplat.uri`。`REQ-077-b` 会把 canonical 表面收敛到 `gaussianSplat.uri`，并让保存后的 scene 不再把 splat 当成 mesh。

## 参考资料

| 资料 | 用途 |
|---|---|
| [GraphDeco-INRIA gaussian-splatting](https://github.com/graphdeco-inria/gaussian-splatting) | 原始参考实现和论文入口 |
| [PlayCanvas: The PLY Format](https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/ply/) | 3DGS PLY 字段和运行时限制概览 |
| [NVIDIA Kaolin `kaolin.io.ply`](https://kaolin.readthedocs.io/en/latest/modules/kaolin.io.ply.html) | `f_dc_*`、`f_rest_*`、`opacity`、`scale_*`、`rot_*` import/export 约定 |
| [Voxel51 Gaussian Splatting dataset](https://huggingface.co/datasets/Voxel51/gaussian_splatting) | 本仓库样例资产来源 |

## 继续阅读

- assets-downloader catalog：`src/tools/assets-downloader/catalog/default.yaml`
- [REQ-077-a: 3DGS PLY Loader And CPU Resource](../../requirements/077-a-3dgs-ply-loader-and-resource.md)
- [REQ-077-c: 3DGS Vulkan Splat Pass](../../requirements/077-c-3dgs-vulkan-splat-pass.md)
- [Vulkan Backend](../../subsystems/vulkan-backend.md)
- 历史基础：`REQ-063-a` Compute Pipeline Foundation

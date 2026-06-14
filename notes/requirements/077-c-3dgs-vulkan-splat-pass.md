# REQ-077-c: 3DGS Vulkan Splat Pass

> 2026-06-14 重排：本需求原为 `REQ-063-b`，现按当前主线后置到 `REQ-077-c`。通用 compute foundation 已由 finished `REQ-063-a` 完成；当前仍没有 3DGS CPU resource、GPU upload、splat shader/pass、排序或 editor 可视化闭环。

## 背景

3DGS 渲染的核心不是画三角形，而是把每个 3D Gaussian 投影到屏幕形成 2D 椭圆 splat，再按近似体渲染公式做透明度合成。高质量实现通常需要 GPU 排序、tile binning 和 EWA-like footprint。首版应选择能在当前 Vulkan 架构里稳定落地的最小路径。

调研结论见 `notes/roadmaps/research/3dgs-ply-rendering/02-渲染算法.md`。Open3D 使用 GPU 投影 / tile sort / composite pipeline；WebGL viewer 常用 worker 深度排序。LXEngine 首版可以先采用 CPU view-depth sort。通用 compute pipeline 支撑已经作为历史前置完成，但本 REQ 不要求一开始就实现 tile binning 或 GPU radix sort。

## 目标

1. 把 3DGS CPU resource 上传成 GPU buffers。
2. 提供首个 splat render path。
3. 支持 DC 颜色和 opacity，可先不启用高阶 SH。
4. 消费 finished `REQ-063-a` 的 compute pipeline foundation，为后续 GPU sort / composite 留出接口。
5. 保证样例 scene 可见、可 orbit、可截图。

## 需求

### R1: Compute foundation 前置

本 REQ SHALL NOT 自行补通用 compute pipeline 基建。实现前应复核 finished `REQ-063-a` 已提供：

- compute pipeline 描述。
- `vkCreateComputePipelines` 路径。
- `vkCmdDispatch` 封装。
- storage buffer binding。
- 同 queue compute barrier。

这些能力由 finished `REQ-063-a` 提供；本 REQ 只消费，不重写通用 compute foundation。

### R2: GPU Buffer 布局

Backend SHALL 为 splat 数据创建 GPU buffers。首版可采用 SoA 或 AoS，但 layout SHALL 在 spec / shader 中明确记录。

至少包含：

- position
- scale
- rotation
- opacity
- base color / `f_dc`

### R3: Shader

新增 3DGS shader。首版 SHALL：

- 根据 camera VP 投影 splat center。
- 从 `scale` + `rotation` 推导屏幕空间 footprint。
- 用 alpha blending 合成颜色。
- 支持 viewport resize。

首版 MAY 只使用 `f_dc`，但 SHALL 保留 `f_rest` 接入点，不把 SH 数据从 CPU resource 丢弃。

Shader 输入 SHALL 以 activated values 为主：

| Shader input | Source |
|---|---|
| linear scale | loader 对 `scale_*` 做 exp |
| normalized rotation | loader normalize `rot_*` |
| alpha | loader 对 `opacity` 做 sigmoid |
| base color | `f_dc_0..2` |

### R4: 排序策略

首版 SHALL 明确一种排序策略：

- CPU 每帧按 view depth 排序，或
- GPU path 有等价可验证的 back-to-front 合成顺序。

如果先采用 CPU 排序，文档 SHALL 标记性能边界，并为后续 tile/binning 留出接口。

首版默认策略：

| Item | Requirement |
|---|---|
| Sort key | view-space depth of splat center |
| Order | back-to-front for alpha blending |
| Frequency | camera changes or transform changes trigger resort |
| Performance note | 741k splat 样例必须可交互；更大场景不作为首版验收 |

### R5: Render pass 集成

3DGS pass SHALL 接入当前 render loop / FrameGraph 事实。它 SHALL 与 mesh forward pass 共存，不破坏现有 shadow、debug draw、ImGui overlay。

首版 pass 位置 SHOULD 是 mesh forward 之后、post-process 之前；如果当前 FrameGraph 尚未支持声明式插入，则实现 SHALL 在现有 render loop 中保持同等顺序，并在文档中标记后续迁移点。

### R6: 验收

验收 SHALL 使用：

- 小型 in-repo fixture scene 作为自动化默认输入。
- assets-downloader cache URI `cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/converted/point_cloud.ply` 作为本地大型 smoke 输入。

目标：

- scene 打开后能看到 splat cloud。
- camera orbit 后画面持续可见。
- 截图不为空。
- resize 后不崩溃。

## 修改范围

- `src/backend/vulkan/`
- `src/core/rhi/`
- `src/core/frame_graph/`
- `assets/shaders/glsl/`
- `src/demos/lxe_editor/`
- `src/test/`

## 依赖

- `REQ-076-c`
- `REQ-077-b`
- finished `REQ-063-a`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/frame-graph/spec.md`
- `notes/roadmaps/research/3dgs-ply-rendering/02-渲染算法.md`

## 实施状态

2026-06-14 重排后状态：保留 active，未实施。

通用 compute foundation 已由 finished `REQ-063-a` 完成并归档；本 REQ 仍缺 3DGS CPU resource、GPU buffer upload、splat shader/pass、排序和 editor 可视化闭环。

# REQ-061-a: 3DGS PLY Loader And CPU Resource

> 2026-05-28 新增：实现 GraphDeco-style 3DGS PLY 的 CPU 解析层，为后续 GPU 资源和渲染 pass 提供稳定数据合同。

## 背景

当前 mesh-loading spec 只覆盖 OBJ 和 glTF。3DGS PLY 不能进入 `Mesh`，因为它没有 index buffer、triangle primitive 或 mesh material。它需要独立的 loader 和资源类型，避免把 splat 数据伪装成三角网格。

调研结论见 `notes/roadmaps/research/3dgs-ply-rendering/01-格式与资产.md`。PlayCanvas、Kaolin 和 GraphDeco-style 数据都指向同一组核心字段：`f_dc_*`、`f_rest_*`、`opacity`、`scale_*`、`rot_*`。

## 目标

1. 解析 binary little-endian 3DGS PLY。
2. 识别必要属性：`x/y/z`、`f_dc_0..2`、`opacity`、`scale_0..2`、`rot_0..3`。
3. 保留可选属性：`f_rest_0..44`、`nx/ny/nz`。
4. 产出 CPU 侧 `GaussianSplatCloud` 资源，包含 splat count、bounds 和 SH degree。

## 需求

### R1: Loader 类型

新增 `GaussianSplatPlyLoader`，建议位置：

- `src/infra/gaussian_splat_loader/`

Loader SHALL 不依赖 Vulkan。它只负责解析文件和填充 CPU 数据。

### R2: Header 校验

Loader SHALL 校验：

- magic 为 `ply`
- format 为 `binary_little_endian 1.0`
- 存在 `element vertex N`
- 必要属性全部存在
- 所有被消费属性为 32-bit float

缺少必要属性时 SHALL 抛出 `std::runtime_error`，错误信息包含文件路径和缺失字段。

### R3: 数据布局

CPU resource SHALL 至少保存：

| Field | Meaning |
|---|---|
| `position` | Gaussian center |
| `fDc` | 0 阶 SH / base RGB |
| `fRest` | 可选高阶 SH，最多 45 float |
| `opacity` | raw opacity logit |
| `scale` | raw log-scale |
| `rotation` | raw quaternion |

Loader SHALL 记录 source layout 到 runtime layout 的映射，不依赖固定属性顺序。

CPU resource SHALL 明确区分 raw storage 与 activated view：

| Value | Raw PLY | Activated view |
|---|---|---|
| opacity | logit-like float | sigmoid 后 alpha |
| scale | log-scale float3 | exp 后 linear scale |
| rotation | unnormalized quaternion | normalized quaternion |
| color | SH coefficients | first pass 可只读取 `f_dc` |

首版 MAY 在加载时预计算 activated opacity / scale / rotation，以降低每帧 shader 成本；但 SHALL 保留 raw 值，便于调试和后续导出。

### R4: Bounds 和统计信息

加载完成后 SHALL 计算 local-space bounds，并暴露 splat count、SH degree、是否包含 normal placeholder。

### R5: 测试覆盖

测试 SHALL 覆盖：

- `assets/models/3dgs_train_sample/point_cloud.ply` 可解析。
- vertex count 等于 `741883`。
- 缺字段 PLY 抛出清晰错误。
- 属性顺序变化时仍可按 header 映射读取。

### R6: 当前代码对照

实现 SHALL 不修改 `ObjLoader` / `GLTFLoader` 的语义。当前 `scene_builder.cpp::loadModelMesh()` 只支持 `.obj`、`.gltf`、`.glb`，3DGS PLY loader SHALL 作为新路径接入，而不是扩展 `Mesh` loader 返回伪 mesh。

## 修改范围

- `src/infra/gaussian_splat_loader/`
- `src/core/asset/` 或合适的 core resource 目录
- `src/test/`
- `openspec/specs/` 中新增或扩展 3DGS PLY loader spec

## 依赖

- `REQ-060-a`
- `openspec/specs/cpp-style-guide/spec.md`
- `notes/roadmaps/research/3dgs-ply-rendering/01-格式与资产.md`

## 实施状态

Draft，未实施。

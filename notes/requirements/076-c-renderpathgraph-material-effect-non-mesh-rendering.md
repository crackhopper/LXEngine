# REQ-076-c: RenderPathGraph / Material / Effect Non-Mesh Rendering Extension

> 原 `REQ-059-a` 重排并改写。当前不再保留“论文实验 sandbox”作为 active 目标；本 REQ 聚焦扩展现有 RenderPathGraph、material、effect 架构，使它能承载 3DGS 等非 mesh 渲染结构，而不是把它们硬塞进 Mesh / SurfaceMaterial。

## 背景

当前代码已经具备 RenderPathGraph / RenderFeature 解析、SceneResourceTable、typed handle、upload view、source-local material record 等基础。实时渲染路径仍主要围绕 mesh、surface material、draw item 和图形 pass 组织。

3DGS 这类结构不是 triangle mesh，也不是基于 BSDF 的 surface material。它需要独立的资源类型、上传视图、pass 声明、backend dispatch 和诊断规则；同时又应该继续复用 RenderPathGraph 的 pass 依赖、effect/feature 管理、material source contract 和 frame graph 边界。

本 REQ 是 `REQ-077-*` 3DGS 链条的架构前置，不实现 3DGS loader、编辑器 UI 或最终渲染算法。

## 目标

1. 定义非 mesh renderable 在 RenderPathGraph 中的声明方式。
2. 让 SceneResourceTable 能表达 Gaussian splat / point-cloud-like 资源，而不是伪装成 Mesh。
3. 明确 SurfaceMaterial 与 Effect / RenderFeature 的职责边界。
4. 让 RenderWorkQueue / backend dispatch 能路由非 mesh work item。
5. 给 `REQ-077-a` 到 `REQ-077-e` 提供可执行的前置契约和验证点。

## 需求

### R1: Render primitive taxonomy

引入渲染 primitive 分类，至少区分：

- `mesh_surface`
- `non_mesh_splat`
- future `volume` / `curve` / `procedural`

要求：

- 分类出现在 core 层可验证的数据结构中，而不是只存在 backend switch。
- mesh 渲染的默认行为不变。
- 非 mesh primitive 不要求 `MeshHandle`、`MaterialInstance` 或 surface pipeline identity。

### R2: SceneResourceTable non-mesh resource

SceneResourceTable 支持非 mesh 资源记录，例如 `GaussianSplatCloud` 或更通用的 `RenderPrimitiveResource`。

要求：

- 资源拥有 typed handle。
- upload view 能暴露 backend 所需的 buffer / texture / metadata。
- 资源生命周期遵循现有 RAII / factory 规则。
- loader 或 parser 尚未实现时，也能通过 mock resource 覆盖验证。

### R3: RenderPathGraph pass contract

RenderPathGraph / RenderFeature 能声明 pass 消费非 mesh source。

要求：

- pass source / target 验证能识别非 mesh 资源类型。
- 缺失资源、资源类型不匹配、unsupported primitive 给出明确诊断。
- pass 依赖仍由 RenderPathGraph / FrameGraph 管理，不新增旁路调度器。

### R4: Material / Effect boundary

SurfaceMaterial 继续表达 BSDF、surface shading 和 material instance。非 mesh 渲染可以由 RenderFeature / Effect 或专用 source 拥有 shader、参数和 pipeline。

要求：

- 不新增 material-local pass fallback。
- 不把 3DGS 参数写成 SurfaceMaterial property。
- effect-owned 参数需要可序列化、可验证，并能被 pass 编译阶段读取。
- `MaterialShaderSource` 仍只服务 material source contract；非 mesh shader source 使用显式 effect/source contract。

### R5: RenderWorkQueue and backend dispatch

RenderWorkQueue 支持非 mesh work item。

要求：

- work item 显式携带 primitive type、resource handle、effect/pass identity 和 dispatch mode。
- backend 可以选择 graphics、compute 或 future ray tracing dispatch。
- 不通过创建 fake mesh、fake material instance 或 dummy vertex buffer 来复用 mesh path。
- Vulkan backend 的未实现路径必须 fail fast，并返回可定位诊断。

### R6: Validation and diagnostics

新增验证覆盖：

- graph 中声明 splat pass 但 scene 缺少 splat resource。
- graph 中 pass 要求 non-mesh resource，但实际绑定 mesh。
- effect/source 参数 schema 不匹配。
- backend 不支持指定 primitive / dispatch mode。

诊断应包含 graph path、pass id、resource id 和 primitive type。

### R7: 3DGS handoff

完成本 REQ 后，`REQ-077-*` 应只需要实现：

- PLY / Gaussian splat asset loader。
- splat resource 上传。
- 3DGS render feature / pass。
- editor 导入和调参。
- offline / realtime 对比验证。

如果 3DGS 实现仍需要修改 mesh/material 基础模型，说明本 REQ 的架构边界没有完成。

## 修改范围

- `src/core/render/`
- `src/core/scene/`
- `src/core/material/`
- `src/core/pipeline/`
- `src/backend/vulkan/`
- RenderPathGraph / RenderFeature parser
- SceneResourceTable / upload view
- RenderWorkQueue / FrameGraph bridge
- tests / diagnostics

## 边界与约束

- 本 REQ 不实现 3DGS loader。
- 本 REQ 不实现具体 3DGS 排序、tile binning 或 splat shader。
- 本 REQ 不引入独立于 RenderPathGraph 的研究 sandbox。
- 本 REQ 不把非 mesh 资源伪装成 mesh。
- 本 REQ 不改变 Material v3 的 source contract 目标。

## 测试覆盖

- RenderPathGraph parser 能接受一个 mock non-mesh pass。
- 资源类型不匹配时返回明确诊断。
- SceneResourceTable 能注册 mock non-mesh resource 并生成 upload view。
- RenderWorkQueue 能生成 non-mesh work item。
- backend unsupported dispatch 能 fail fast。
- mesh_surface path 回归测试不变。

## 依赖

- `REQ-073-e`
- `REQ-073-f`
- `REQ-073-g`
- `REQ-073-h`
- `REQ-075-a`

历史上 `REQ-067-a`、`REQ-071-b`、`REQ-071-c` 提供了 SceneResourceTable 和 RenderPathGraph 基础，但它们已归档，不再作为 active 前置。

## 后续工作

- `REQ-077-a`: 3DGS asset loader and scene resource
- `REQ-077-b`: 3DGS render feature and Vulkan pass
- `REQ-077-c`: editor import and inspection workflow
- `REQ-077-d`: offline / realtime comparison for splats
- `REQ-077-e`: source analysis and subsystem notes

## 实施状态

2026-06-14 重排后状态：保留 active，作为 `REQ-077-*` 的架构前置。当前代码已有 RenderPathGraph、RenderFeature、SceneResourceTable 和 material/effect 基础，但非 mesh primitive contract、resource taxonomy、work item routing 和 backend dispatch 边界尚未完成。

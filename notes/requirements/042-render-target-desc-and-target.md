# REQ-042: RenderTarget 拆分为 descriptor 与 binding，引入 MRT / layer / pipeline-key 接入

## 背景

当前 `src/core/frame_graph/render_target.hpp` 中的 `RenderTarget` 是早期占位实现：

- 三个字段（`colorFormat` / `depthFormat` / `sampleCount`）
- `operator==` 与 `getHash`
- 不持有任何 GPU 资源
- 不参与 [`PipelineKey`](finished/002-pipeline-key.md) 组合

这套现状有四类问题：

1. **角色冲突** — 类型名 `RenderTarget` 暗示"绑定（持有 attachment 句柄）"，
   但内容只表达"形状（descriptor）"。下游 `Camera::matchesTarget`、
   `Scene::getSceneLevelResources`、`RenderQueue::buildFromScene` 已经在依赖它，
   但都只是在比形状，目前没有办法表达"我画进哪个具体 framebuffer"。

2. **字段缺口** — 多 color attachment（MRT）、layer count、stencil 拆分、
   load/store ops、clear values、自定义 extent、resolve attachment 全部没有；
   现有 `sampleCount` 字段存在但没人消费，`> 1` 不会真的出 MSAA。

3. **identity 路径错** — `getHash` 是 runtime hash，没有任何生产调用方
   （仅一个自测在测它本身），与仓库已确立的 string interning identity 路径
   （[`REQ-006`](finished/006-string-table-compose.md) /
   [`REQ-007`](finished/007-interning-pipeline-identity.md)）不一致。同时
   `RenderTarget` 不进 `PipelineKey`，意味着不同 attachment format 的两个 pass
   在 core 层会被算成同一条 pipeline，与 Vulkan 的 renderpass 兼容性规则冲突。

4. **REQ-009 target 轴是占位 hook** — [`REQ-009`](finished/009-multi-camera-multi-light.md)
   的两轴筛选已经在 API 形状上落地，但 target 轴在工程实际数据上几乎没有真实
   过滤：所有 pass / seed Camera 都默认构造，`matchesTarget` 永远返回 true。
   target 轴长什么内容、怎么对比，要等 RenderTarget 长出真字段后才能确定。

本 REQ 用文档先于代码的方式收口 RenderTarget 的设计，把上述四类问题整合为
一次类型拆分 + 字段补齐 + identity 路径接入 + Camera 比较语义澄清的统一变更。

## 在 Frame Graph 演进中的位置

本 REQ 是 *frame graph 演进的字段层前置*。具体定位：

- **本 REQ 不引入 frame graph 的任何新机制**（不构边、不做拓扑、不推 barrier、不做 aliasing、不数据驱动）—— 这些由后续候选 REQ-035..041 分阶段引入
- **本 REQ 的产物是后续 frame graph 演进的 *attachment 形状描述基础***：`RenderTargetDesc` 是 attachment 在 frame graph 边上的载体；没有它，"input attachment" / "output attachment" 都没法表达
- **本 REQ 与 `Camera::matchesTarget` 的语义升级是 frame graph 演进的副产品**：让 REQ-009 *target 轴* 摆脱当前的占位 hook 状态

完整 frame graph 演进路径见
[`notes/roadmaps/research/frame-graph/06-演进路径.md`](../roadmaps/research/frame-graph/06-演进路径.md)。
当前 `FrameGraph / FramePass / RenderQueue / RenderTarget` 在源码层面的导读见
[`notes/source_analysis/src/core/frame_graph/`](../source_analysis/src/core/frame_graph/render_target.md)。

**严格说，R1-R8 应在以下任一条件命中之前完成**：

- 立项 [REQ-119 G-Buffer / 延迟渲染](../roadmaps/main-roadmap/phase-1-rendering-depth.md#req-119--g-buffer--延迟渲染路径)
- 立项 [REQ-103 Shadow Pass](../roadmaps/main-roadmap/phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline)
- 立项候选 frame-graph REQ `FrameGraphResource` 数据结构

否则后续 REQ 会持续在"占位 RenderTarget"上搭建，最后回头改要付额外迁移成本。

## 与 Phase 1.5 的时序关系（2026-05-01）

[Phase 1.5 ImGui Editor MVP](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 在 2026-05-01 立项后插入到 Phase 1 之前。本 REQ 的实施时间窗口是 **Phase 1.5 完工后、Phase 1 [REQ-103 Shadow Pass](../roadmaps/main-roadmap/phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) 立项前**。

理由：

1. **Phase 1.5 不强触发本 REQ**：7 个 REQ 都不要求 RenderTarget 持有真 GPU 句柄。[REQ-041 编辑器 MVP](041-imgui-editor-mvp.md) R6 显式选择"加 `Camera::m_active` 布尔"路径切换 game/editor 相机，避开 picture-in-picture 视口（picture-in-picture 才是本 REQ 的真触发点）。
2. **REQ-103 强触发本 REQ**：shadow map 是典型跨 pass 资源（depth-only pass 写 → forward pass 采样）。没有真 GPU 资源 + attachment format 进 PipelineKey，REQ-103 在数据结构上无法干净落地。
3. **Phase 1.5 与本 REQ 的兼容预留**：[REQ-037-b Camera 作为 component](037-b-camera-as-component.md) 完全不动 `CameraComponent::m_data.target` / `matchesTarget`；[REQ-039 DebugDraw](039-debug-draw-subsystem.md) 注册 `Pass_DebugOverlay` 时按当时 RenderTarget API 写，未来本 REQ 改 `FramePass` 字段时一并更新即可；[REQ-041](041-imgui-editor-mvp.md) 的 `m_active` 与 `m_target` 解耦，互不干扰。三者已在自身边界段落标注 *REQ-042 兼容预留*。
4. **R9 已拆出且已完成**：原 R9 删 `getHash` cleanup 由独立的 [REQ-034](finished/034-remove-render-target-get-hash.md) 承担，已归档完成。本 REQ 可直接假设该 cleanup 已存在。

## 目标

1. 把 `RenderTarget` 拆为 `RenderTargetDesc`（intern-friendly 形状）和
   `RenderTarget`（持有 desc + IGpuResource 句柄 + extent）两类。
2. 让 `RenderTargetDesc` 通过 `getPipelineSignature()` 接入 `PipelineKey` 第三级
   compose，与全仓 string interning identity 路径一致。
3. 让 v1 字段表足够支撑 forward + 简单 MRT + 多 layer 路径，同时给 MSAA / resize
   留可扩展位但不实现。
4. 把 `Camera::matchesTarget` 的语义从 "比单一 RenderTarget" 升级为
   "比 RenderTargetDesc 形状兼容"，nullopt 表示通配，让多 swapchain 路径在 API
   形状上不被阻塞。

> 原本的 R9（删除 `RenderTarget::getHash` 与对应自测）已在 2026-05-01 拆出独立的
> [REQ-034 删除 RenderTarget::getHash dead code](finished/034-remove-render-target-get-hash.md)，
> 不再属于本 REQ 范围。本 REQ 实施时假设 REQ-034 已合入。

## 需求

### R1: 必须拆分 `RenderTargetDesc` 与 `RenderTarget` 两个类型

- `RenderTargetDesc` 只表达 attachment 形状，不持有 GPU 资源，可比可哈希
  （通过 `getPipelineSignature()` 落到 `StringID`），适合 cache key、pipeline
  identity、Camera 兼容性比较。
- `RenderTarget` 持有：
  - `RenderTargetDesc desc`
  - `std::vector<IGpuResourceSharedPtr> colorAttachments`
  - `std::optional<IGpuResourceSharedPtr> depthStencilAttachment`
  - `Extent2D extent`
- 不允许出现"既能描述形状，又能持有句柄"的单一类型。
- 命名采用 `Desc` 后缀，与仓库现有的 `PipelineBuildDesc` 一致；不使用 `Layout`
  以避免与 descriptor set layout / vertex layout 概念混淆。

### R2: `RenderTargetDesc` 字段表

首版必须包含以下字段：

```cpp
struct ColorAttachmentDesc {
  ImageFormat format;
  LoadOp loadOp;          // CLEAR / LOAD / DONT_CARE
  StoreOp storeOp;        // STORE / DONT_CARE
};

struct DepthStencilAttachmentDesc {
  ImageFormat format;     // 允许 packed (D24S8 / D32S8) 与 pure-depth / pure-stencil
  bool hasDepth = true;
  bool hasStencil = false;
  LoadOp depthLoadOp;
  StoreOp depthStoreOp;
  LoadOp stencilLoadOp;
  StoreOp stencilStoreOp;
};

struct RenderTargetDesc {
  std::vector<ColorAttachmentDesc> colorAttachments;          // v1 长度 ≥ 1
  std::optional<DepthStencilAttachmentDesc> depthStencil;     // 可选
  u32 layerCount = 1;                                          // multi-view 预留
  u8 sampleCount = 1;                                          // > 1 显式不支持
};
```

- 字段顺序与 `getPipelineSignature` 中 compose 顺序保持一致，便于审阅。
- `loadOp` / `storeOp` 进 desc，因为 Vulkan renderpass 兼容性规则把它们算入兼容性。
- `clearValue` 不进 desc（见 R3），它是运行期数据，不影响 pipeline / renderpass 兼容性。

### R3: clear values 在 binding 层，不在 desc

`RenderTarget`（binding 层）持有：

```cpp
std::vector<ClearValue> colorClearValues;     // 与 colorAttachments 一一对应
std::optional<ClearValue> depthStencilClearValue;
```

理由：

- clear value 不参与 pipeline 兼容性
- clear value 可以每帧改（动态天空色、调试色）
- 把 clear value 放进 desc 会让 cache 失效频率与 *实际 pipeline 兼容性变化*
  完全脱钩

### R4: `getPipelineSignature` 必须返回稳定的 `StringID`

- `RenderTargetDesc::getPipelineSignature() const -> StringID` 通过
  `GlobalStringTable::compose(TypeTag::RenderTarget, {...})` 组装，
  与仓库其他 `getPipelineSignature` 接口（mesh、material_template、
  material_pass_definition、shader）同构。
- compose 必须覆盖：所有 colorAttachments 的 `(format, loadOp, storeOp)`、
  `depthStencil`（若存在）的 `(format, hasDepth, hasStencil, depthOp, stencilOp)`、
  `layerCount`、`sampleCount`。
- 字段未来扩展时，必须同步更新 `operator==` 与 `getPipelineSignature`，否则会
  出现 `==` 相等但 signature 不等（破坏 cache 命中）或反之（cache 假命中）。

### R5: `PipelineKey` 必须扩为三级 compose

把 [`REQ-002`](finished/002-pipeline-key.md) /
[`REQ-007`](finished/007-interning-pipeline-identity.md) 中定义的
`PipelineKey::build(objectSig, materialSig)` 升级为：

```cpp
static PipelineKey build(StringID objectSig, StringID materialSig, StringID targetSig);
```

- `targetSig = renderTargetDesc.getPipelineSignature()`
- `RenderQueue::buildFromScene` 在组装 `RenderingItem::pipelineKey` 时必须传入
  当前 pass 的 `RenderTargetDesc::getPipelineSignature()`
- 这会让相同材质 + 相同几何在不同 attachment format 的两个 pass 里被算成两条
  pipeline；这是显式化"pipeline 与 attachment format 强绑"的硬约束，不是回归

### R6: `Camera::matchesTarget` 必须改为比 `RenderTargetDesc`

- `Camera::m_target` 类型从 `std::optional<RenderTarget>` 改为
  `std::optional<RenderTargetDesc>`。
- `Camera::matchesTarget(const RenderTargetDesc &desc)` 返回 `m_target == nullopt
  || *m_target == desc`，nullopt 表示通配（覆盖所有 desc）。
- `Scene::getSceneLevelResources(pass, desc)` /
  `Scene::getCombinedCameraCullingMask(desc)` /
  `RenderQueue::buildFromScene(scene, pass, desc)` 的 `target` 参数类型同步改为
  `const RenderTargetDesc &`。

### R7: GPU 资源 ownership 必须走 IGpuResource

- `RenderTarget` 持有的所有 attachment 资源必须是 `IGpuResourceSharedPtr`，
  与 [`gpu_resource`](../source_analysis/src/core/rhi/gpu_resource.md) 路径一致。
- backend（Vulkan）通过 `IGpuResource` 适配器暴露 swapchain image 与 offscreen
  image；core 层不直接接触 `VkImage` / `VkImageView`。
- `RenderTarget::create(desc, attachments, extent)` 接受已经构造好的
  `IGpuResourceSharedPtr` 列表；构造路径与"谁创建 attachment"解耦。

### R8: MSAA 与 swapchain resize 必须显式标记为 v1 不支持

- 若 `sampleCount > 1`：`RenderTarget::create` 必须 `FATAL + terminate`，错误
  消息明确指向 REQ-035+。
- swapchain resize 路径在 v1 不实现：`RenderTarget` 实例创建后字段不可变；
  resize 触发 `FrameGraph` 重建（具体协议留 REQ-035+ 收口）。
- 这两个限制写在头文件 doc-comment 里以及 `getPipelineSignature` 的实现旁边，
  避免后续误用。

### R9 已移出本 REQ

R9（删除 `RenderTarget::getHash` 与对应自测）原属本 REQ 的"前置 cleanup"。在 2026-05-01 决定拆分后，独立成 [REQ-034](finished/034-remove-render-target-get-hash.md)。该 cleanup 已归档完成；本 REQ 不再重复列出该工作。

## 测试

- `RenderTargetDesc::getPipelineSignature()` 对相同字段稳定返回相同 `StringID`，
  字段任何一处变化（包含 colorAttachments 列表长度、stencil 拆分状态、
  layerCount、loadOp、storeOp）都必须产生不同 `StringID`
- `PipelineKey::build(objSig, matSig, targetSigA) != PipelineKey::build(objSig,
  matSig, targetSigB)` 当 targetSig 不同
- `Camera::matchesTarget(desc)` 在 `m_target == nullopt` 时返回 true
- `Camera::matchesTarget(descA)` 在 `m_target == descA` 时返回 true，在
  `m_target == descB`（descA != descB）时返回 false
- `Scene` 默认 seed 出的 Camera + 默认 `RenderTargetDesc{}` 在所有内置 `Pass_*`
  上仍能命中 scene-level 资源（保持 [`REQ-009`](finished/009-multi-camera-multi-light.md)
  默认兜底）
- 创建 `sampleCount > 1` 的 `RenderTarget` 必须 `FATAL + terminate`
- MRT desc（colorAttachments 长度 ≥ 2）能成功构造、能产生与单 attachment 不同的
  `getPipelineSignature`
- 含 packed depth+stencil（D24S8）的 `DepthStencilAttachmentDesc` 能正确编码到
  `getPipelineSignature` 中（与 pure-depth 区分）

## 修改范围

- `src/core/frame_graph/render_target.hpp` / `.cpp`（拆类型、补字段）
- `src/core/pipeline/pipeline_key.hpp` / `.cpp`（三级 compose）
- `src/core/scene/camera.hpp` / `.cpp`（`m_target` 类型 + matchesTarget 签名）
- `src/core/scene/scene.hpp` / `.cpp`（getSceneLevelResources / getCombinedCameraCullingMask 参数类型）
- `src/core/frame_graph/render_queue.hpp` / `.cpp`（buildFromScene 参数类型 + 注入 targetSig）
- `src/core/frame_graph/frame_graph.hpp` / `.cpp`（FramePass::target 类型，可能需要加 desc 字段）
- `src/test/integration/*` 中所有传 `RenderTarget` 给 Scene/Camera/RenderQueue 的测试 setup
- `notes/source_analysis/src/core/frame_graph/render_target.md`（落地后重写）
- `notes/source_analysis/src/core/scene/scene.md`（target 轴叙事更新）
- `notes/source_analysis/src/core/frame_graph/render_queue.md`（buildFromScene 签名 + targetSig 注入）
- `openspec/specs/`（若有 RenderTarget / PipelineKey 相关 spec，同步更新）

## 边界与约束

- 本 REQ 不引入 MSAA 真实支持（resolve attachment、resolve mode、subpass 多采样
  策略），仅保留字段并显式拒绝 `> 1`；真实 MSAA 留 REQ-035+
- 本 REQ 不引入 swapchain resize 协议；v1 RenderTarget 实例不可变；resize 触发
  整体重建的协议留 REQ-035+
- 本 REQ 不引入 layer count > 1 的真实使用（cubemap shadow、cascade、stereo VR）；
  仅保留字段并参与 signature；v1 默认 1
- 本 REQ 不引入 pipeline 兼容性"compatible 而非 identical"的概念（Vulkan 允许
  attachment formats 相同的 pipeline 跨 renderpass 复用）；v1 走严格相等路径，
  优化留 REQ-035+
- 本 REQ 不重新定义 `IGpuResource` 接口；attachment 复用其现有契约，必要时
  backend 内部加 image-as-attachment 适配器即可
- 本 REQ 不引入 multi-window / multi-swapchain 真实支持，但 API 形状不阻塞 —
  `RenderTargetDesc` 不持有 swapchain 实例引用，desc 一致即可跨 swapchain 复用

## 依赖

- [`REQ-002`](finished/002-pipeline-key.md) — PipelineKey 两级 compose（被本 REQ 升级为三级）
- [`REQ-006`](finished/006-string-table-compose.md) — GlobalStringTable::compose 接口
- [`REQ-007`](finished/007-interning-pipeline-identity.md) — interning identity 路径，本 REQ 沿用
- [`REQ-008`](finished/008-frame-graph-drives-rendering.md) — FrameGraph 驱动渲染，本 REQ 修改 FramePass / RenderQueue 签名
- [`REQ-009`](finished/009-multi-camera-multi-light.md) — REQ-009 的 target 轴在本 REQ 后才有真实负载
- [`REQ-026`](finished/026-camera-visibility-layer-mask.md) — Camera 可见性掩码不变，但 Camera::m_target 类型同步改

## 后续工作

### Frame Graph 演进（候选 REQ-035..041）

本 REQ 是 *frame graph 演进的字段层前置*。完整路径见
[`notes/roadmaps/research/frame-graph/06-演进路径.md`](../roadmaps/research/frame-graph/06-演进路径.md)：

- 候选 REQ-035：`FrameGraphResource` 数据结构 + 跨 pass 资源命名（代码 API）
- 候选 REQ-036：compile v1 — 构边 + 拓扑排序 + cycle 检测
- 候选 REQ-037：自动 renderpass / framebuffer 创建
- 候选 REQ-038：自动 barrier 推导（**风险点 #1**）
- 候选 REQ-039：JSON parser + schema（数据驱动）
- 候选 REQ-040：attachment aliasing（**风险点 #2**）
- 候选 REQ-041：runtime register_pass 策略热替换

注意：上述编号为候选，正式立项时按 `notes/requirements/` 实际可用号顺延。

### RenderTarget 自身的延伸（非 frame graph 路径）

- 真实 MSAA 支持（resolve attachment、subpass 策略）—— 本 REQ R8 显式拒绝 `sampleCount > 1`，真实支持留 follow-up
- swapchain resize 协议（backend 通知 / FrameGraph 重建 / cache 失效边界）—— 本 REQ R8 不实现 resize，留 follow-up
- multi-view rendering 真实路径（cubemap shadow、cascade、stereo）—— 本 REQ R2 字段层支持 `layerCount > 1`，但不消费
- multi-window / multi-swapchain 路径 —— 本 REQ API 不阻塞但不实现
- pipeline 兼容性放宽（"compatible 而非 identical"）以提高 cache 复用率 —— 本 REQ R5 走严格相等路径，优化留 follow-up

## 实施状态

待实施。本 REQ 是文档先于代码的设计收口，单阶段（R1..R8）实施：

- 时间窗口：Phase 1.5 七个 REQ（[035](finished/035-transform-component.md) ~ [041](041-imgui-editor-mvp.md)）全部落地后开工；必须早于 Phase 1 [REQ-103 Shadow Pass](../roadmaps/main-roadmap/phase-1-rendering-depth.md#req-103--shadow-pass--depth-only-pipeline) 立项
- 前置 cleanup：[REQ-034](finished/034-remove-render-target-get-hash.md) 已删除 `getHash` dead code，不再阻塞本 REQ 任何决策
- R1..R8 在一个或一组连续 PR 中完成，每个 PR 必须保持工程可编译可测
- 落地后重写 `notes/source_analysis/src/core/frame_graph/render_target.md`，把"未完成蓝图"叙事换成成型设计的源码导读

详细时序见 [与 Phase 1.5 的时序关系](#与-phase-15-的时序关系2026-05-01) 段落。

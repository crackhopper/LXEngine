# REQ-071-f: Helmet / BMW Direct Lighting Validation

> 2026-06-10 新增：本 REQ 是 `REQ-071` 连续需求族的第六步。目标是在 material v2、technique/pass/effect、SceneResourceTable、GPUResourceTable、bindless + indirect draw 和 scene package 完成后，用 helmet 与 BMW M6 验证材质系统、资源管理和 Forward / Deferred / OfflineRT 直接光照一致性。

## 背景

`REQ-071-a` 到 `REQ-071-e` 分别完成 material v2、technique/pass/effect、SceneResourceTable、GPUResourceTable 和 scene package。本 REQ 是收口验收，不再引入新的材质合同或资源架构。

本轮只关注：

- helmet/BMW 场景中实际存在的每个材质实例都能进入 direct lighting 验证。
- Forward、Deferred、OfflineRT 在受控 direct profile 下输出可比较结果。
- source scene 加载和 package restore 都能驱动同一条渲染链路。
- headless realtime 渲染入口与 editor 打开场景后导出渲染结果保持一致。
- image compare 工具能给出可定位的材质/物体/像素诊断，而不是只输出全图误差。

Shadow、IBL、Transparent/glass pass 不属于本 REQ 验收范围。它们在 validation profile 中必须关闭；如果 scene 或材质配置中存在相关 pass/effect，当前 technique 应输出 disabled/unsupported diagnostic，而不是静默参与 direct lighting 对比。

## 目标

1. 创建或更新 helmet 与 BMW M6 validation scene，使它们使用 material v2、三 technique 和 scene package。
2. 为两个模型中实际存在的每个材质实例生成 64x64 材质小球 direct validation。
3. 用 128x128 完整模型 direct validation 验证真实场景链路、FrameGraph、bindless + indirect draw 和 debug dump。
4. 扩展现有 EXR 相似度比较工具，使其支持 diagnostic buffers、per-material metrics、edge/coverage 分类和 top-N suspicious samples。
5. 提供 realtime headless/CLI 渲染入口，并验证它与 editor scene export 走同一渲染核心、输出一致。
6. 审计 `REQ-071-a` 到 `REQ-071-e` 的临时桥接，确保默认 smoke 不依赖旧材质、legacy descriptor 或非 bindless draw path。

## 需求

### R1: Render Validation Controls

scene SHALL 支持 validation profile，把测试控制与普通运行配置分开。

示例：

```yaml
renderValidation:
  sourceMode: source # source | package
  activeCamera: validation_main
  outputProfile: lowres-direct
  randomSeed: 1
  toneMapping: linear
  compare:
    materialMasks: true
    diagnostics: true

realtimeRender:
  activeTechnique: Forward
  directLighting: true
  environmentLighting: false
  alphaTransparency: false
  shadows: false
  debugDumps:
    frameGraphTargets: true
    gbuffer: true

offlineRender:
  integrator: direct
  directLighting: true
  environmentLighting: false
  randomSeed: 1
  samplesPerPixel: 1
  toneMapping: linear
```

规则：

- validation profile SHALL 关闭 shadow、IBL、transparent/glass pass。
- `sourceMode` 控制从 source scene 解析还是从 `.lxpkg` package restore。
- `randomSeed`、camera、resolution、tone mapping、sample count 必须固定，保证 offline/realtime direct 对比可复现。
- `realtimeRender.activeTechnique` 可切换 `Forward` / `Deferred`；`OfflineRT` 由 offline profile 驱动。
- 如果 active technique 缺失某材质需要的 pass，该对象不渲染并输出 warning；不能使用隐式 shader/pass 兜底。
- `REQ-071-e` 已负责 source parse 与 package restore 的 SceneResourceTable Merkle hash 和 offline bit-exact 验证；本 REQ 只验证完整渲染链路。

### R2: Helmet / BMW Validation Assets

helmet 与 BMW M6 SHALL 提供 validation-ready 资产。

要求：

- 材质全部为 material v2。
- 每个材质声明 `Forward`、`Deferred`、`OfflineRT` technique。
- mesh、texture、camera、light、effect 均由 SceneResourceTable 管理。
- source scene 和 `.lxpkg` package 都可作为输入。
- 默认 validation profile 使用低分辨率 direct lighting，关闭 shadow、IBL、transparent。
- BMW 中的 glass 材质本轮不验证透明 pass；它仍必须有 direct-lighting test representation，用于验证材质参数、资源绑定和直射输出，不验证折射、透明排序或座椅可见性。

### R3: Material Sphere Direct Validation

每个 helmet/BMW 中实际存在的 `MaterialInstance` SHALL 生成一个 64x64 材质小球 validation case。

规则：

- 使用固定 UV sphere、固定 sampler、固定 light、固定 camera、固定 tone mapping。
- 保留真实 texture sample 路径，验证资源绑定和采样一致性。
- 分别渲染 Forward direct、Deferred direct、OfflineRT direct。
- 材质小球是 per-material direct lighting 一致性的主要数值验收。
- 所有实际存在的材质实例都必须参与；不按 BSDF type 豁免。
- 若某材质的真实透明、折射、shadow、IBL 行为不在本轮支持范围，validation 使用 direct-lighting test representation，并输出 diagnostic 说明哪些真实效果被禁用。

### R4: Material Diagnostic Channels

材质小球和完整模型 validation SHALL 输出小尺寸 diagnostic buffers。

固定通道：

| 通道 | 用途 |
|---|---|
| `color` | direct lighting 输出 |
| `materialId` | material instance id |
| `objectId` / `drawId` | 完整模型定位物体和 draw |
| `normal` | shading/geometric normal，按 profile 固定 |
| `depthOrVisibility` | 背景、覆盖和有效像素判断 |
| `directInputsHash` | 光照、camera、采样后材质输入等 direct 计算输入摘要 |

template-specific debug channels 由 MaterialTemplate 定义，不使用旧 PBR 泛名兜底。

示例：

| Template | Debug channels |
|---|---|
| `matte` | `Kd`、`sigma` |
| `substrate` | `Kd`、`Ks`、`uroughness`、`vroughness` |
| `metal` | `eta`、`k`、`roughness` |
| `glass` | `Kr`、`Kt`、`eta`，以及 direct test representation 标记 |
| `mix` | `amount`、resolved child ids、child contribution summary |
| `fourier` | `bsdfTableId`、supported/unsupported direct test representation info |

要求：

- diagnostic data 使用 EXR channel 或 typed binary buffer，不保存 per-pixel JSON。
- 材质小球 diagnostic 固定 64x64。
- 自动完整模型 validation 固定 128x128；更高分辨率只作为手动诊断 profile。
- 对比程序按 MaterialTemplate debug layout 解释通道。

### R5: Diagnostics-aware Compare Tool

现有 `src/tools/lxe_compare_exr/` SHALL 扩展为 diagnostics-aware compare 工具，复用已有 `meanAbsError`、`maxAbsError`、`rmse`、`linearL1/srgbL1 similarPixelRatios` 和 gate 脚本能力。

新增能力：

- 输入 color EXR 和辅助 diagnostic buffers。
- 输出全图 metrics、per-material metrics、per-object/draw metrics。
- 对差异像素分类：
  - `edge/coverage mismatch`：depth、normal、visibility、materialId 在邻域变化剧烈，通常是边缘或 rasterization 覆盖差异。
  - `input mismatch`：directInputsHash 或 template debug channel 不一致，说明材质参数、texture sample、资源绑定或坐标输入不同。
  - `BRDF mismatch`：输入一致但 color 不一致，说明公式或 direct lighting 实现不同。
  - `unsupported/disabled`：该材质或 pass 在当前 profile 中被明确禁用或不支持。
- 输出 top-N suspicious samples / regions，包含像素坐标、diff 值、material instance id/URI、object id、mesh/draw id、分类原因和 debug channel 差异摘要。
- gate 脚本 SHALL 主要按 `material interior mismatch` / `BRDF mismatch` 判失败；edge/coverage mismatch 单独统计，避免把物体边缘误报为材质公式错误。

边缘分类要求：

- 工具 SHALL 基于 depth、normal、materialId、visibility 的 3x3 或 5x5 邻域变化生成 edge mask。
- edge mask 内像素不参与材质公式阈值，只进入 edge/coverage 统计。
- 如果 edge/coverage mismatch 过大，测试可以失败，但错误类型必须区别于 material BRDF mismatch。

### R6: Direct Lighting Equivalence

direct lighting 对比 SHALL 分层执行。

材质小球：

- Forward direct vs Deferred direct。
- Forward direct vs OfflineRT direct。
- Deferred direct vs OfflineRT direct。
- 按 material instance 统计 mean error、p95 error、max error、有效像素数、非黑像素比例和分类后的 failure samples。
- coverage 不足时报告 coverage failure，不能直接判通过。

完整模型：

- helmet/BMW 使用 128x128 direct profile。
- 渲染 Forward direct、Deferred direct、OfflineRT direct。
- 使用 diagnostics-aware compare 工具复用现有 EXR 相似度指标。
- 完整模型做场景级相似度和链路 smoke，不承担每个材质公式定位的主要责任。
- 若完整模型差异超阈值，报告应指向 material sphere 结果、per-material metrics、debug dumps 和 suspicious regions。

### R7: Full Model Smoke And Debug Dumps

helmet/BMW 完整模型 validation SHALL 验证真实渲染链路。

要求：

- `sourceMode=source` 和 `sourceMode=package` 都能加载。
- Forward direct 和 Deferred direct 都能完成渲染。
- 默认使用 bindless + indirect draw；不得触达 legacy descriptor 或非 bindless draw path。
- 输出非全黑。
- 材质覆盖统计显示场景中存在的 material instance 至少在 materialId/debug dump 中出现；若 coverage 不足必须报告。
- GBuffer / FrameGraph dump 有效：
  - albedo / normal / material / depth / lighting target 存在。
  - dump 文件名或 metadata 包含 logical target id/version。
  - 每个 source target 都有 FrameGraph producer。
  - 同一物理 attachment 被多次写入时，dump 使用 SSA-style logical target，不按物理 attachment 覆盖。

### R8: Realtime Headless Validation Entry

realtime renderer SHALL 提供 headless/CLI validation 入口。

要求：

- 支持参数：scene/package 路径、sourceMode、active technique、camera、output profile、debug dump 开关、输出路径。
- 可以跳过 editor UI、交互循环、swapchain present。
- 不得绕过核心渲染环节：
  - SceneResourceTable source parse 或 package restore。
  - Material v2。
  - technique/pass validation。
  - FrameGraph compile。
  - GPUResourceTable upload。
  - bindless + indirect draw。
  - realtime renderer executor。
- headless 输出必须和“正常打开 scene 后执行导出渲染结果命令”的输出做图像对比。
- 如果 headless 与 editor export 不一致，测试失败，说明 CLI path 存在 workaround 或 editor path 存在状态污染。

### R9: Unsupported / Disabled Feature Diagnostics

Shadow、IBL、Transparent/glass pass 不在本 REQ 验收范围内。

要求：

- validation profile 中这些功能必须关闭。
- 如果 scene/effect/material 中配置了相关 pass，当前 validation run 应输出 disabled diagnostic。
- 它们不参与 direct lighting 阈值。
- 后续需求再验证 shadow correctness、IBL 质量、transparent pass、glass depth/write/visibility 和座椅可见性。

### R10: 071 Temporary Bridge Audit

验收 `REQ-071-a` 到 `REQ-071-e` 期间为保持 helmet smoke 可用而记录的所有临时桥接。

要求：

- 输出 bridge audit report。
- 每个 bridge 要么已删除，要么已改写成最终 contract。
- 如果仍有未完成能力，必须转成新的后续 REQ，不得留在 071 主线内作为隐式兼容路径。
- 旧材质定义、旧 runtime PBR 参数、legacy descriptor 兼容路径不得继续作为 smoke 通过条件。
- 非 bindless transitional draw path 应已在 `REQ-071-d` 清理；本 REQ 只做审计，不接收“留到 F 再清”的债务。
- 默认 editor/offline/headless tests 必须走 bindless + indirect。

## 测试

### T1: Material Sphere Direct Equivalence

对 helmet/BMW 中每个 material instance 生成 64x64 小球：

- Forward direct、Deferred direct、OfflineRT direct 都输出 color + diagnostic buffers。
- diagnostics-aware compare 输出 per-material metrics。
- 所有 material instance 都有有效 coverage。
- 输入一致但 color 超阈值时报告 BRDF mismatch。
- 输入不一致时报告 input mismatch 和对应 debug channel。

### T2: Full Model Direct Similarity

helmet/BMW 使用 128x128 validation profile：

- Forward direct、Deferred direct、OfflineRT direct 输出 EXR。
- 使用扩展后的 `lxe_compare_exr` / gate 脚本比较。
- 输出全图 metrics、per-material metrics、edge/coverage 统计和 suspicious samples。
- 不把 edge/coverage mismatch 误报为材质公式错误。

### T3: Source vs Package Render Chain

同一 scene 分别使用 `sourceMode=source` 和 `sourceMode=package`：

- 两者都能完成 realtime headless 渲染。
- 两者都能完成 editor export 渲染。
- 两者都能完成 offline direct 渲染。
- package correctness 的 Merkle hash 和 offline bit-exact 由 `REQ-071-e` 覆盖；本测试只确认 package restore 后能进入完整渲染链路。

### T4: Headless vs Editor Export

同一 scene、technique、camera、profile：

- headless realtime 输出与 editor 打开 scene 后导出的 realtime 输出一致或在既定阈值内。
- 两者 debug metadata 显示相同 technique、FrameGraph、pipeline key、resource table root hash 和 render settings hash。
- 不允许 headless path 使用不同 shader、不同 resource binding 或绕过 swapchain 之外的渲染逻辑。

### T5: Deferred GBuffer / FrameGraph Dump

Deferred 渲染 BMW/helmet：

- albedo、normal、material、depth、lighting dump 存在。
- GBuffer target 非全黑，depth 有有效范围，normal 合法。
- lighting pass 输入 target 均来自 FrameGraph producer。
- dump 使用 logical target id/version。

### T6: Disabled Feature Diagnostics

validation profile 中 Shadow、IBL、Transparent/glass pass 关闭：

- direct lighting 仍工作。
- 如果 scene/effect/material 声明了相关 pass，输出 disabled/unsupported diagnostic。
- 这些 feature 不参与 direct equivalence 阈值。

### T7: 071 Temporary Bridge Audit

验证 bridge audit report：

- 默认 validation 未调用 legacy material loader。
- 默认 validation 未调用 legacy per-material descriptor path。
- 默认 validation 未调用非 bindless draw submission。
- 如保留 debug-only path，必须默认关闭，并有测试确认 validation 未使用它。

## 修改范围

- `assets/scenes/`：helmet/BMW validation scene 与 renderValidation profile。
- `assets/materials/`：material v2 helmet/BMW 材质与 direct test representation。
- `assets/shaders/glsl/common/` 与 `techniques/`：direct BRDF common 函数、template-specific debug channel 输出。
- `src/tools/lxe_compare_exr/`：diagnostics-aware compare、per-material metrics、edge/coverage 分类、top-N suspicious samples。
- `src/tools/lxe_offline_render/`：direct/offline profile、deterministic output、diagnostic dump 接入。
- `src/tools/lxe_realtime_render/` 或 `src/demos/lxe_editor/`：headless realtime validation entry。
- `src/demos/lxe_editor/`：scene export render command 与 validation profile 支持。
- `src/backend/vulkan/`：GBuffer/FrameGraph dump、headless render target、debug metadata。
- `src/test/`：material sphere、full model similarity、headless/editor export、diagnostics compare、bridge audit 测试。

## 边界与约束

- 本 REQ 的硬性等价目标是低分辨率直接光照。
- 本 REQ 不验收 Shadow、IBL、Transparent/glass pass。
- 本 REQ 不要求完整 PBRT spectral renderer。
- 本 REQ 不要求 Fourier BSDF 精确渲染，但必须保留数据并为 direct test representation 输出明确 diagnostic。
- 本 REQ 不实现复杂 OIT。
- 本 REQ 不以“看起来差不多”替代 diagnostic buffers、FrameGraph dump 和 compare report。
- 材质小球 validation 固定 64x64；自动完整模型 validation 固定 128x128。

## 依赖

- `REQ-071-a`：material v2 PBRT surface contract。
- `REQ-071-b`：technique/pass/effect/FrameGraph contract。
- `REQ-071-c`：SceneResourceTable parser/resource abstraction。
- `REQ-071-d`：GPUResourceTable、pipeline cache、bindless + indirect draw 和 upload tasks。
- `REQ-071-e`：scene package fast load、Merkle hash 和 source/package offline equivalence。
- `REQ-070-a`：BMW M6 converter。
- `REQ-068-a`：EXR 输出 profile 与 `lxe_compare_exr` 基础指标。

## 后续工作

- Shadow correctness。
- IBL/environment lighting visual 和数值验证。
- Transparent/glass pass、depth write、排序、座椅可见性。
- 完整 PBRT spectral metal eta/k shader。
- PBRT glass refraction / rough dielectric。
- Fourier BSDF table evaluation。
- mesh 合批与 draw call 优化。

## 实施状态

未实施。本文档用于确认 071 需求族完成后的 direct lighting、材质系统和资源管线验收标准。

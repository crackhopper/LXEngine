# REQ-071-b: RenderPathGraph / RenderPassNode / RenderFeature 与 FrameGraph Contract

> 2026-06-14 归档：RenderPathGraph、RenderPassNode、RenderFeature 与 FrameGraph contract 已经成为当前代码基础；剩余 Material v3 hard cut、OfflineRT graph path、legacy bridge 删除和非 mesh 渲染扩展分别由 `REQ-073-*` / `REQ-076-b`、`REQ-076-c/d`、`REQ-076-i` 承接。

> 2026-06-10 新增：本 REQ 是 `REQ-071` 连续需求族的第二步。目标是在 `REQ-071-a` 的 PBRT `SurfaceMaterial` pure envelope 参数层之上，定义显式 `RenderPath`、`RenderPathGraph`、`RenderPassNode`、`RenderFeature`、source/target 和 FrameGraph 构建合同，删除代码内部对 pass 枚举、shader、target 和依赖关系的隐式兜底。

## 背景

当前渲染路径仍残留大量“按代码约定”而非“按资产声明”的关系：

| 当前问题 | 影响 |
|---|---|
| material pass 与 runtime pass 枚举耦合 | 新增 Deferred/OfflineRT 时容易缺 pass 或走错 shader |
| shader / target / render state 可由代码默认推导 | 出错时难以定位，材质文件看不出真实渲染流程 |
| G-Buffer、deferred lighting、透明、IBL、post-process 的关系不统一 | FrameGraph 无法根据资源依赖自动建立 DAG |
| 不绑定具体物体的算法参数没有独立 envelope | shadow map、GBuffer lighting、tone mapping、IBL resolve 等逻辑被塞进 renderer 代码 |

我们现在采用 pure envelope 模型：`.material` 只表达 `SurfaceMaterial` 参数；`RenderFeature` 只表达算法/效果参数；所有 pass、shader、source/target、render state、资源依赖和 pass 间关系统一由 `RenderPathGraph` 表达。FrameGraph 根据当前激活的 `RenderPath` 和对应 graph，遍历 scene/resource table，按标准 target/source 建立 DAG，并组织 render work item。

## 目标

1. `.material` v2 不声明 `defaultTechnique` / `techniques`；`SurfaceMaterial` 保持 pure envelope。
2. 新增 `RenderPath` 概念，表示 Forward、Deferred、OfflineRT 这类顶层渲染框架。
3. 新增 `RenderPathGraph` 文件，显式声明某个 RenderPath 的 pass DAG、shader、stage、dispatch、sources、targets、renderState、feature/material 依赖和 pass filters。
4. 新增 `RenderFeature` 文件，用于 shadow map、SSAO、GI、tone mapping、IBL resolve 等算法/效果参数；它也是 pure envelope，不声明 pass 或 shader。
5. 每个 `RenderPassNode` 显式声明 shader、stage、dispatch、sources、targets、renderState。
6. 新增 render path graph validation 层，校验 graph 下所有 pass 的 shader reflection、layout、material/filter 匹配和 pipeline 支持性。
7. FrameGraph 基于标准 graph target/source 建 DAG，去除硬编码 pass 枚举和依赖关系。
8. 全局/相机切换 RenderPath 时，graph 根据 `RenderClass`、BSDF type 和 feature set 选择 pass；不支持的对象不渲染并打印 warning。
9. 固定系统 ABI 统一从 UBO 迁移到 SSBO 数组，并由 C++ mirror struct 与 shader common 文件共同定义、reflection 校验。

## 命名与边界

| 名称 | 定义 |
|---|---|
| `RenderPath` | Forward、Deferred、OfflineRT 等顶层渲染框架；它选择一张或一组 RenderPathGraph |
| `RenderPathGraph` | 某个 RenderPath 的 pass DAG 资产，声明 pass、shader、source/target、resource、feature/material 依赖和 render state |
| `RenderPassNode` | RenderPathGraph 中的单个 pass 节点，例如 `ForwardOpaque`、`ShadowMap`、`GBuffer`、`DeferredLighting`、`Transparent`、`ToneMap` |
| `RenderFeature` | shadowmap、SSAO、GI、bloom、tone mapping、TAA 等算法/效果的 pure envelope 参数 |
| `FeatureEnvelope` | `RenderFeature` 的参数表，与 material envelope 类似，允许常量和 typed resource URI |
| `RenderClass` | SurfaceMaterial / object 的分类标签，例如 `surface.opaque`、`surface.transparent`，供 RenderPathGraph pass filter 使用 |

`SurfaceMaterial` 和 `RenderFeature` 都是参数 envelope；它们不拥有 shader、pass 或 render state。`RenderPathGraph` 是唯一描述 shader 绑定和 pass 关系的资产。

## 需求

### R1: RenderPathGraph 文件合同

`RenderPathGraph` SHALL 使用独立文件表达。建议扩展名为 `.render-path.yaml` 或 `.render-path-graph.yaml`，最低合同：

```yaml
schema: lxe.render-path-graph.v1
name: ForwardMain
renderPath: Forward

imports:
  resources:
    - graph.resources.common

features:
  shadow:
    uri: effects/shadow.render-feature.yaml

resources:
  imported:
    - scene.camera
    - scene.lights
    - scene.objects
  graph:
    shadow.main:
      type: depthTexture
      producer: ShadowMap
    hdr.color.direct:
      type: colorTexture
      producer: ForwardOpaque

passes:
  - id: ShadowMap
    stage: raster
    dispatch: draw
    shader: techniques/Forward/shadow_depth_only
    filters:
      renderClass: [surface.opaque, surface.alpha-test]
    sources: [geometry.vertex, geometry.index, scene.objects, scene.camera, feature.shadow]
    targets: [shadow.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      blendEnable: false

  - id: ForwardOpaque
    stage: raster
    dispatch: draw
    shader: techniques/Forward/surface_lit
    filters:
      renderClass: [surface.opaque]
      bsdf: [matte, uber, metal, substrate]
    sources: [geometry.vertex, geometry.index, material.bsdf, scene.camera, scene.lights, shadow.main]
    targets: [hdr.color.direct, depth.main]
    renderState:
      cullMode: Back
      depthTest: true
      depthWrite: true
      blendEnable: false
```

每个 RenderPath 可以有一张 graph，也可以由一个更大的 graph 文件包含多个 `renderPaths` 分支。无论组织方式如何，pass/shader/renderState 只能在 `RenderPathGraph` 中声明，不能回写到 `.material` 或 `.render-feature.yaml`。

scene / camera / renderer SHALL 指定 active `RenderPath`；未指定时使用 renderer default。Material 不提供 default render path。

### R2: RenderPassNode 字段全显式

每个 `RenderPassNode` SHALL 显式声明：

| 字段 | 说明 |
|---|---|
| `shader` | shader asset URI，不允许代码按 pass 名推导 |
| `stage` | `raster` 或 `compute` |
| `dispatch` | `draw`、`fullscreen`、`compute` 等 |
| `sources` | 标准 graph source 列表 |
| `targets` | 标准 graph target 列表 |
| `renderState` | cull/depth/blend/color write 等完整固定功能状态 |

缺任一字段 SHALL fail-fast。代码不得自动补 shader、target、blend、depthWrite 或 render pass。

### R3: RenderPathGraph Validation

新增 render path graph validation 层，在 scene load 或 render path switch 时校验 active graph。

它 SHALL 校验：

- active RenderPath 是否存在对应 RenderPathGraph。
- graph 内每个 pass 的 shader URI 存在。
- shader reflection 与 pass sources/targets 兼容。
- pass filters 至少能匹配 `RenderClass`、BSDF type、object visibility / domain。
- vertex layout / descriptor layout / push constants / render state 能由现有 pipeline 系统自动创建。
- pass 的 source target 是否来自标准 registry。
- 当前 backend 是否支持该 pass 的 stage/dispatch。
- `SurfaceMaterial` envelope 是否能满足对应 pass shader 的 material-owned reflection 需求。
- `RenderFeature` envelope 是否能满足对应 pass shader 的 feature-owned reflection 需求。

如果某个 object/material 的 `RenderClass` 或 BSDF type 没有被 active graph 支持，该对象不进入 render queue，并打印 warning。  
如果 graph 声明了 pass 但 shader/source/target/renderState 不合法，应 fail-fast，因为这是作者错误。

### R4: Shader 目录按 RenderPath 组织

shader 源码 SHALL 按 render path 组织：

```text
assets/shaders/glsl/common/
assets/shaders/glsl/techniques/Forward/
assets/shaders/glsl/techniques/Deferred/
assets/shaders/glsl/techniques/OfflineRT/
```

公用 BSDF、Fresnel、direct lighting、packing/unpacking 函数放 `common/`。Forward、Deferred、OfflineRT 的 shader 应尽可能复用 common 函数，避免同一 BRDF 公式在 realtime/offline 分叉。目录名保留 `techniques/Forward` 等现有过渡路径时，语义上 SHALL 解释为 RenderPath 目录，不再表示 material-local technique。

### R5: RenderFeature 文件合同

不绑定具体 renderable 的算法/效果参数 SHALL 使用 `RenderFeature` 文件表达。`RenderFeature` 是参数 envelope，不声明 pass、shader、phase 或 render state。

`RenderFeature` SHALL 声明：

- `schema: lxe.render-feature.v1`
- `name`
- `feature`
- `parameters`
- 可选 `resources` / typed URI envelope，用于 blue noise、LUT、environment map 等 feature 参数依赖。

示例：

```yaml
schema: lxe.render-feature.v1
name: MainShadow
feature: shadowmap
parameters:
  resolution:
    kind: integer
    value: 2048
  bias:
    kind: float
    value: 0.001
  cascadeCount:
    kind: integer
    value: 1
```

camera / renderer scene YAML SHALL 支持引用 RenderPathGraph 和 RenderFeature：

```yaml
camera:
  renderPath: Forward
  renderPathGraph:
    uri: render_paths/forward_main.render-path.yaml
  features:
    shadow:
      uri: effects/shadow.render-feature.yaml
    toneMapping:
      uri: effects/tone_mapping.render-feature.yaml
```

### R6: RenderPathGraph 驱动的 FrameGraph 构建

FrameGraph 首版 SHALL 从 active `RenderPathGraph` 构建 DAG。Forward 可以是单 pass 或多 pass；Deferred/OfflineRT 可以拥有更多 pass。是否存在 GBuffer 由 graph 声明，不由 RenderPath 名称隐式推导。

`RenderPathGraph` 中每个 `RenderPassNode` 通过 `sources` / `targets` 表达依赖。`RenderFeature` 只提供参数，不能单独形成 pass；例如 shadowmap 的 pass 是 graph 里的 `ShadowMap` 节点，它读取 `feature.shadow` 参数 envelope。

规则：

- pass 可以依赖 camera、scene、geometry、material、feature envelope、imported resources 和更早 pass target。
- pass 之间如果依赖 target，依赖关系必须通过 source/target 显式表达。
- 单 pass Forward graph 可以只有一个 `ForwardOpaque` / `ForwardTransparent` 节点或两个节点；它不需要 GBuffer。
- 透明 pass 必须在 graph 中显式声明依赖，例如依赖 opaque color/depth target，不能由 material alpha 隐式插入。
- 多个 RenderPathGraph 可以引用同一 RenderFeature 或同一 shared graph resource，例如 Forward/Deferred 都引用同一个 `ShadowMap` pass library 或 `shadow.main` 资源合同。

### R7: 标准 Graph Source / Target Registry

新增标准 source/target registry。pass source/target 不是自由字符串。

首版 target 至少包含：

| Target | 用途 |
|---|---|
| `depth.main` | 主深度 |
| `gbuffer.albedo` | Deferred diffuse/base color |
| `gbuffer.normal` | normal/roughness |
| `gbuffer.material` | material id、BSDF type、参数索引 |
| `gbuffer.emissive` | emissive |
| `hdr.color` | linear HDR color |
| `ldr.color` | tone mapped color |
| `swapchain.color` | present target |
| `shadow.main` | shadow map |
| `environment.radiance` | environment/IBL input |

首版 source 至少包含：

| Source | 用途 |
|---|---|
| `geometry.vertex` / `geometry.index` | draw geometry |
| `material.bsdf` | material v2 参数 |
| `camera.ubo` | camera data |
| `scene.lights` | lights |
| `scene.bvh` | offline ray tracing acceleration |
| `scene.environment` | environment input |

如果 source 没有 producer，也不是 imported resource，FrameGraph build SHALL fail-fast。

### R7.1: Target 写入使用版本化逻辑名字

FrameGraph 中的 `target` 是逻辑资源标记，不等同于底层 GPU buffer/image。为了用 `sources` / `targets` 构建 dependency graph，逻辑 target SHALL 使用 SSA-style versioned identity。

规则：

- 一个逻辑 target id 在同一 FrameGraph 中默认只能有一个 producer。
- 如果同一底层 image/buffer 被多个 pass 写入，每次写入 SHALL 使用不同的逻辑 target id，例如 `hdr.color.direct`、`hdr.color.transparent`、`hdr.color.tonemappedInput`。
- source 必须引用明确的逻辑 target id，不能只引用模糊的底层 buffer 名。
- FrameGraph 可以在 compile 后把多个逻辑 target alias 到同一底层 attachment，但 aliasing 是 backend/resource allocation 优化，不改变 dependency graph 的逻辑名字。
- 多个 pass 写同一个逻辑 target id SHALL fail-fast，除非该 target registry 明确支持 `blend` / `append`，且 pass 显式声明 `writeMode`。
- 不允许隐式“最后一个 pass 覆盖前一个 pass”的规则。

### R8: Deferred Lighting 按 BSDF / SurfaceMaterialTemplate 组织

Deferred lighting pass SHOULD 按 `SurfaceMaterialTemplate` / BSDF type 组织 lighting work，而不是每个 mesh 重复光照。

首版可接受：

- GBuffer 几何 pass 仍按对象 draw。
- lighting pass 读取 GBuffer，按 BSDF type/material id 分组或分支。
- 对暂不支持的 BSDF type，输出 unsupported diagnostic；该对象或 pass 不应静默渲染为黑。

### R9: 透明 SurfaceMaterial 规则

透明 pass SHALL 在 `RenderPathGraph` 中被显式声明，不再由颜色 alpha 或材质名隐式触发。

规则：

- glass/transparent forward pass 在 graph 中声明 `depthWrite: false`，启用 blend 或后续 OIT 策略。
- deferred opaque GBuffer 不处理透明玻璃，除非 RenderPathGraph 声明专门 transparent pass。
- 透明 pass 在 DAG 中位于 opaque lighting 之后，除非 RenderPathGraph 显式声明其他策略。

### R10: 固定系统 ABI 的 C++ / GLSL Common Contract

固定系统变量 SHALL 与 material/effect 自定义变量分开管理。

固定系统 ABI 包括但不限于：

- camera / frame data。
- light array。
- object array。
- draw/instance data。
- geometry stream descriptors。
- material instance indirection table。
- skinning/bone data。

这些结构 SHALL：

- 在 C++ 侧保留明确 mirror struct。
- 在 shader 侧集中写入 `assets/shaders/glsl/common/` 下的 common include 文件。
- C++ mirror 文件和 GLSL common 文件 SHALL 使用相同 file stem，便于人工查找和工具校验。例如 `scene_system_abi.hpp` 对应 `scene_system_abi.glsl`，二者定义同一组固定 ABI struct / binding 名称。
- 第一阶段统一使用 SSBO 数组，不再把 camera/light/object 等系统数据设计成 per-draw UBO。
- 使用固定 set/binding 编号，并通过 shader reflection 校验 block 名称、set/binding、descriptor type、array/struct member、offset、size 与 C++ mirror struct 一致。
- 作为 system-owned binding，不出现在 `.material` 或 `.render-feature.yaml` 的 material-owned 参数表里。

示例命名：

```text
SceneFrameData
SceneCameraData[]
SceneLightData[]
SceneObjectData[]
SceneDrawData[]
SceneGeometryStreamData[]
SceneMaterialInstanceData[]
SceneTextures
```

具体字段由实现阶段按现有 renderer/offline shader 需求确定，但本 REQ 要求字段定义集中、可反射校验、不可在各 shader 中复制粘贴出多份同名不同 layout 的结构。

这些固定 ABI block / binding 名称 SHALL 是保留字。用户 material shader 或 RenderFeature shader 如果声明同名 binding，则该 binding 必须完全匹配引擎 common ABI；不能把保留名用于自定义参数。冲突或 layout 不匹配 SHALL fail-fast，并报告 shader、binding name、expected ABI 和 reflected ABI。

固定 ABI 的 descriptor set / binding 编号 SHALL 也是 contract 的一部分。系统级 scene/object/material/geometry/texture table 使用固定 scene set；material/feature-owned 参数使用独立 set/binding namespace，由 reflection 自动创建或校验。用户 shader 不得占用系统固定 set/binding 来声明自定义资源。

### R11: SurfaceMaterial / RenderFeature 变量仍走 Reflection

除 R10 的固定系统 ABI 外，SurfaceMaterial 和 RenderFeature 自己声明的变量 SHALL 继续走 shader reflection。

要求：

- material-owned buffer/texture/image/sampler 由 shader reflection 得到 layout 和 binding，并由 `SurfaceMaterial` envelope 提供数据。
- feature-owned 参数同理通过 reflection 得到 layout，并由 `RenderFeature` envelope 提供数据。
- graph validation 校验 material/feature 文件是否提供所需参数或资源。
- pipeline/descriptor layout 可以由 reflection 自动构建或校验。
- 代码不得为用户 shader 新增临时 C++ 类来映射自定义参数。

## 测试

### T1: RenderPathGraph Validation

测试缺 shader、缺 renderState、非法 target、unsupported dispatch 都会 fail-fast。

### T2: Unsupported RenderPathGraph Coverage

scene 指定 `Deferred`，active graph 不支持某 object 的 `RenderClass` / BSDF type：

- 该对象不渲染。
- 控制台输出 warning，包含 material URI、object/node 名、active RenderPath 和缺失的 render class / BSDF 支持。
- scene 继续加载。

### T3: FrameGraph DAG

构造 RenderPathGraph：

- `ShadowMap -> ForwardOpaque`
- `GBuffer -> DeferredLighting -> ToneMapping`
- 验证 compile 后执行顺序满足依赖。

### T4: No Hidden Fallback

删除 pass shader 或 target，验证不会由代码自动补默认 shader 或 swapchain target。

### T5: Transparent Glass

构造 glass material：

- pass 显式 `depthWrite: false`。
- opaque object behind glass 可见。
- glass pass 顺序在 opaque pass 之后。

### T6: System ABI Reflection Validation

构造使用 `SceneCameraData`、`SceneLightData`、`SceneObjectData` 的 shader：

- reflection 显示它们都是 StorageBuffer / SSBO。
- C++ mirror struct 的 size/offset 与 shader block 成员一致。
- shader 中把固定 ABI 错写成 UBO 或改字段 offset 时测试失败。

### T7: Effect-owned Reflection Parameters

构造一个 post feature shader，声明 feature-owned parameter block：

- RenderFeature 文件提供参数。
- RenderPathGraph validation 通过 reflection 校验。
- 缺字段或类型错误时 fail-fast。

### T8: Helmet Rendering Smoke Gate

本 REQ 完成时 SHALL 继续运行 `REQ-071-a` 定义的 helmet editor/offline smoke：

- helmet scene 使用 Material v2。
- active RenderPath 至少覆盖 `Forward`，并验证 RenderPathGraph / RenderFeature validation 不破坏渲染。
- editor realtime 输出非全黑。
- offline direct 输出非全黑。
- 如果为了维持 smoke 通过引入临时 pass/feature bridge，必须记录到当前实施状态，并在 `REQ-071-c` 或 `REQ-071-d` 的后续重构事项中标出清理点。

在 `REQ-071-d` 之前，允许存在 transitional per-object draw path 来保持 smoke 可用，但它必须消费 Material v2 pure envelope、RenderPathGraph validation、SceneResourceTable 数据和固定系统 ABI，不能回退到旧 material loader、旧 PBR 参数或旧 descriptor 兼容路径。

## 修改范围

- `src/core/asset/`：RenderPathGraph、RenderPassNode、RenderFeature envelope、pass definition、render state contract。
- `src/core/frame_graph/`：source/target registry、DAG compile、RenderPathGraph build plan。
- `src/core/scene/` / `src/core/rhi/`：固定系统 ABI mirror struct。
- `src/infra/material_loader/`：移除 material-local technique/pass 解析，保留 SurfaceMaterial pure envelope。
- `src/infra/resource_parsers/`：RenderPathGraphParser、RenderFeatureResourceParser。
- `src/infra/scene_io/`：camera / renderer 的 render path graph 和 feature 引用。
- `assets/shaders/glsl/`：按 technique/common 组织，集中固定系统 ABI common include。
- `assets/materials/`、`assets/effects/`、`assets/render_paths/`：样例 SurfaceMaterial / RenderFeature / RenderPathGraph。
- `src/test/`：render path graph、render feature、FrameGraph DAG、透明 pass 测试。

## 边界与约束

- 本 REQ 不实现完整 IBL；只定义 IBL/environment 作为 source/target 的表达。
- 本 REQ 不要求实现 OIT；透明首版以显式 forward transparent pass 为准。
- 本 REQ 不实现 GPU resource table；pipeline/resource 上传在 `REQ-071-d`。
- 不保留旧硬编码 pass fallback。
- 本 REQ 定义固定系统 ABI 的 SSBO/common/reflection 合同，但 bindless descriptor table 与 indirect draw 执行在 `REQ-071-d`。

## 依赖

- `REQ-071-a`：SurfaceMaterial pure envelope、SurfaceMaterialTemplate 与 MaterialInstance 边界已明确。
- `REQ-067-a`：SceneResourceTable 提供 scene resources 和 object/material handles。
- `REQ-076-j`：Realtime renderer 拆分有助于接入 FrameGraph executor。

## 后续工作

- `REQ-071-c`：统一 parser/manager/resource abstraction，并让 SceneResourceTable 成为 SurfaceMaterial、RenderFeature、RenderPathGraph 的 owner。
- `REQ-071-f`：用 Forward/Deferred/OfflineRT 对比验证 technique contract。
- 清理本 REQ 为保持 helmet smoke 可用而引入的临时 pass/feature bridge。

## 实施状态

2026-06-14 复核：保留 active，部分完成。

当前已经有 `assets/render_paths/*.render-path.yaml`、`RenderPathGraphResourceParser`、`RenderFeatureResourceParser`、RenderPathGraph pass contract 测试、default Forward/Deferred graph source 测试，以及 renderer 对 RenderPathGraph parser 的正向使用。

仍未完成或已拆给后续需求的边界：

- 默认 shader URI / 术语从 `techniques/...` 到 `render_paths/...` 的硬切由 `REQ-073-d` 承接。
- source variant、indirect batching、realtime hard cut 和 OfflineRT graph path 分别由 `REQ-073-c/e/f/g/h` 承接。
- 本文件剩余价值是保留 RenderPathGraph / RenderPassNode / RenderFeature / FrameGraph contract 的 active 对照，不再承载 071 旧验收入口。

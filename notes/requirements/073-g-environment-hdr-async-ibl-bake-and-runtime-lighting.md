# REQ-073-g: Environment HDR Async IBL Bake And Runtime Lighting

> 2026-06-16 校准：本 REQ 属于 `REQ-073` 当前渲染闭环。它接在
> `REQ-073-f` 的 environment / skybox 直接光照之后，把同一个
> `feature.environmentLighting.parameters.environmentMap.uri` 指向的环境 HDR
> 资产异步烘焙成可复用 IBL lighting 资产，并在不重新加载 scene、不重建
> pipeline 的前提下热激活到当前 realtime 渲染。Reflection probe 不在本 REQ
> 范围内，转交 `REQ-073-h`。

## 背景

`REQ-073-f` 已经把可见环境背景收束到 RenderPathGraph +
RenderFeature + SceneResourceTable：环境来源由
`feature.environmentLighting.parameters.environmentMap.uri` 表达，
`builtin:env/white_cube`、HDR / KTX2 EnvMap 和 feature 的 color /
intensity 使用同一条资源路径。

当前 IBL bake 仍有私有后端路径：`IblBakeRenderer` 直接硬编码
`equirect_to_cubemap`、`ibl_irradiance_convolve`、`ibl_prefilter_env` 和
`ibl_brdf_lut` shader，并在 renderer 初始化阶段运行。这个路径能产出资源，
但 bake 输入、输出 metadata、cache、feature 参数、shader reflection 和
runtime activation 不是同一个事实源。

本 REQ 先只解决环境 HDR 的 IBL 闭环：从现有环境光资产生成 diffuse SH、
specular prefiltered cubemap mip chain 和可复用 BRDF LUT，烘焙完成后通过当前
scene 的 `SceneResourceTable` 热激活，并让 Forward 渲染在下一帧直接使用这些
资产产生 environment lighting。Probe capture / local probe lighting 是环境光照
上的扩展，由后续 `REQ-073-h` 复用本 REQ 的 bake 与 activation 管线。

## 目标

1. 提供异步 `bake ibl start` / bake API，返回 `BakeJobId`，不阻塞 editor。
2. 对当前 `feature.environmentLighting.parameters.environmentMap.uri` 的环境 HDR
   资产生成 IBL bake 产物。
3. 将环境配套 bake 产物写到环境资产旁边，供多个 scene 复用。
4. 将 `standard-pbr` BRDF LUT 写到该材质资产旁边，供多个 scene 和环境复用。
5. 烘焙完成后通过 completion callback 在主线程热激活产物到当前
   `SceneResourceTable`，不重新加载 scene，不重建 pipeline。
6. Forward 默认路径在现有 Forward surface pass 内通过
   `feature.surfaceLighting` 控制 IBL contribution，并直接调用 common shader 函数
   消费 baked assets，证明环境光照生效。
7. Deferred 同步接入同一个 common IBL shader / feature / resource contract，
   但本 REQ 不要求 Deferred 图像 smoke 通过。
8. 让 bake job 的日志、进度、失败修复建议可被其他线程读取，并由 editor 同步
   打印到 `editor.log` 和 command prompt。
9. 删除或拒绝 `IblBakeRenderer::bakeStaticEnvironment()` 作为默认正向入口。

## 非目标

- 不实现 `ReflectionProbeComponent`、probe capture、local probe blending 或
  parallax-corrected box projection；由 `REQ-073-h` 处理。
- 不实现 lightmap、DDGI、动态 probe update。
- 不把 Forward 的 IBL 做成额外 geometry/additive pass；Forward 保持单个 surface
  pass 内完成主要 lighting。
- 不解决所有 RenderFeature 参数 hard cut；由 `REQ-073-i` 处理。
- 不实现 package 文件格式；现有 package 需求继续保持独立。

## 需求

### R1: Environment HDR Bake Input

IBL bake SHALL 使用当前 graph / feature 声明的环境资产作为输入。

要求：

- bake 输入来自 live `feature.environmentLighting.parameters.environmentMap.uri`。
- `scene.environment`、`ambientColor`、`ambientIntensity` 等旧 scene 字段不能满足
  bake 输入。
- 如果环境 URI 缺失、文件不存在、格式不支持或 hash 读取失败，bake job failed，
  并输出修复方式与重试命令。
- 缺 cache 不触发 scene load 隐式 bake；用户或 editor 明确调用 `bake ibl start`
  才启动异步 job。

### R2: Async Bake Job Service

IBL bake SHALL 是异步 job。

最低 API / command 行为：

```text
bake ibl start
bake job status <id>
bake job logs <id> [since]
bake job cancel <id>
bake ibl start --force
```

`bake ibl start` 返回 `BakeJobId`。job 在 worker / bake 线程执行耗时工作，并通过
thread-safe event stream 输出状态。

事件最低字段：

| 字段 | 说明 |
|---|---|
| `job` | BakeJobId |
| `phase` | queued / cache-check / filter / write-cache / activate / complete / failed / activation-failed / cancel-pending |
| `severity` | info / warning / error |
| `progress` | 0 到 1 |
| `message` | 用户可读日志 |
| `fix` | 可选修复建议 |
| `sequence` | 单调递增序号，供其他线程增量读取 |

worker 线程不得直接修改 UI 或 editor panel。completion callback 必须 marshal 回
editor/render 主线程，再执行 hot activation。

首版不实现并发队列：全局同一时间只运行一个 IBL bake job。重复执行
`bake ibl start` 时，如果已有 job running，返回已有 `BakeJobId` 或输出已有任务正在
运行的 diagnostic；`--force` 遇到 running job 时 rejected，并提示先 cancel。

默认 `bake ibl start` 先检查 cache：valid cache 直接进入 `activate` phase，仍然
触发 completion callback；invalid cache 输出原因后重新 bake。`--force` 忽略 valid
cache，重新生成当前 scene 收集到的 environment key 和 `standard-pbr` material key。

### R3: Bake Output Layout

环境 bake 产物 SHALL 分为环境配套资产和材质配套 BRDF LUT。

环境资产旁边保存：

| 产物 | 用途 |
|---|---|
| diffuse SH coefficients | diffuse irradiance，例如 SH9 |
| specular prefiltered cubemap mip chain | specular IBL，按 roughness / alpha 查询 mip |
| manifest | source URI/hash、schema、format、resolution、mip count、SH layout、生成参数 |

BRDF LUT 保存到具体材质资产旁边。首版只要求 `standard-pbr`，例如：

```text
assets/materials/standard-pbr/
  standard-pbr.material.yaml
  .lxe-ibl/
    manifest.yaml
    brdf_lut.ktx2
```

LUT key 至少包含 material URI/hash、BRDF/BSDF model、尺寸和格式。多个 scene、
多个 HDR 复用同一个 `standard-pbr` LUT，但它不是独立的全局光源资产。

环境 manifest 合同：

```yaml
schema: lxe.environment-ibl-bake.v1

source:
  uri: assets/env/khronos/neutral/ggx/specular.ktx2
  hash: sha256:...

bake:
  diffuse:
    basis: sh9
  specular:
    format: RGBA16Float
    resolution: 256
    mips: 9
    roughness: alpha-squared
    layout: cubemap
    faces: 6

outputs:
  diffuse:
    file: diffuse_sh9.yaml
  specular:
    file: specular_prefilter.ktx2
```

`bake.specular.mips` 由 `resolution` 计算并记录：`floor(log2(resolution)) + 1`。
首版默认 `resolution=256`，因此记录 `mips=9`。用户配置不需要提供 mip 数。

材质 manifest 合同：

```yaml
schema: lxe.material-ibl-bake.v1

material:
  uri: assets/materials/standard-pbr/standard-pbr.material.yaml
  type: standard-pbr
  hash: sha256:...

bake:
  brdf:
    model: ggx-smith
    format: RG16Float
    size: 256

outputs:
  brdf:
    file: brdf_lut.ktx2
```

Diffuse SH 输出为 YAML：

```yaml
schema: lxe.sh9.v1
space: world
basis: real-sh
order: 2
layout: rgb-interleaved
coefficients:
  - [r, g, b]
```

`coefficients` 必须正好 9 组 RGB。manifest parser 必须 strict：schema、source
hash、`bake` 参数、`outputs` 文件、format、mip、SH layout、BRDF model 缺失或未知
字段都失败。

### R3.1: Bake Key Collection And Deduplication

`bake ibl start` SHALL 从当前 scene 收集需要的 bake key。

要求：

- environment bake key 按 environment source URI/hash 区分；每个 environment
  source 单独 bake。
- material bake key 首版只按 material type 区分；`standard-pbr` 的 GGX/Smith BRDF
  LUT 只 bake 一次。
- 多个物体、多个 `standard-pbr` 材质实例、多个同类型材质不得重复触发 BRDF LUT
  bake。
- 首版不支持非 `standard-pbr` material IBL bake；非支持类型不会阻塞整个 scene。

### R3.2: Bake Render Path Graphs

IBL bake 本身 SHALL 使用独立 render-path YAML 表达，不允许继续把 bake pass 顺序、
shader URI、source/target 和中间资源写死在 C++ 默认路径中。

最低资产：

```text
assets/render_paths/bake_environment_ibl.render-path.yaml
assets/render_paths/bake_standard_pbr_brdf_lut.render-path.yaml
```

要求：

- `bake_environment_ibl.render-path.yaml` 声明 equirect/cubemap 输入、diffuse SH
  输出、prefiltered specular cubemap 输出、中间 target 和 bake shader URI。
- `bake_standard_pbr_brdf_lut.render-path.yaml` 声明 `standard-pbr` BRDF LUT 的
  compute/fullscreen bake 输入、输出、尺寸和格式。
- RenderWorkCompiler 必须从 graph/resource facts 生成 bake work。
- `FrameGraphExecutor` 执行编译后的 graph work；073g 不新增 bake-only executor。
- 缺 source、target、shader、format 或 payload 的 bake graph 必须 rejected，不能由
  backend 补默认值。
- bake service 负责 job、日志、cache、manifest、payload 文件和 atomic commit；
  render-path YAML 不表达文件提交策略。

### R4: Hot Activation Without Scene Reload

bake 或 cache hit 成功后 SHALL 立即热激活到当前 scene。

要求：

- activation 在主线程执行。
- activation 采用两阶段提交：先把新 SH / specular cubemap / BRDF LUT 加载为临时
  live resource handles；全部 payload、descriptor/upload generation 和
  shader-visible facts 准备成功后，再一次性替换当前 `SceneResourceTable` 的 active
  IBL generation。
- 不重新加载 scene。
- 不重建 pipeline。
- pipeline 和 shader ABI 在 bake 前就准备好；bake 完成只改变 resources 和 runtime
  state。
- activation 失败时，磁盘产物保留，当前 active IBL resources 不切换，job 状态为
  `activation-failed`，日志给出修复方式和 `bake ibl start` 重试路径。

### R5: Forward Inline Runtime Consumption

Forward SHALL 在现有 Forward surface pass 内消费 baked assets，不新增默认
`ForwardIblLighting` geometry/additive pass。

首版执行形态：

```text
Forward surface pass:
  material IBL helper
```

`feature.surfaceLighting` SHALL 是 surface pass 的 shared feature，负责提供
Forward / Deferred 都可读的 shader-visible IBL 开关和参数。最低字段：

| 字段 | 说明 |
|---|---|
| `enableIblLighting` | 是否执行 material IBL helper |
| `diffuseIblIntensity` | diffuse IBL contribution multiplier |
| `specularIblIntensity` | specular IBL contribution multiplier |
| `environmentIblReady` | pass-uniform readiness fact，environment SH / cubemap 是否已热激活 |
| `standardPbrIblReady` | pass-uniform readiness fact，`standard-pbr` BRDF LUT 是否已热激活 |

Forward shader 读取这些变量，按开关调用 common 函数；是否开启某段逻辑由
feature data 驱动，不由 C++ hardcoded policy 或 shader macro 双轨决定。shader
允许 pass-uniform readiness branch：

```glsl
if (surfaceLighting.enableIblLighting != 0 &&
    surfaceLighting.environmentIblReady != 0 &&
    surfaceLighting.standardPbrIblReady != 0) {
    color += evaluateIblStandardPbr(...);
}
```

首版支持粒度是 pass + material type：只实现 `standard-pbr` IBL，不做单个材质实例
级别的 baked/unbaked 混合。

RenderWorkCompiler 仍需做 graph/source-level 校验：如果 graph 声明
`feature.surfaceLighting.enableIblLighting=true`，则 `scene.environmentBake`、
`scene.materialIblBake`
和对应 BRDF LUT facts 必须可解析。失败时 graph/work rejected 或该材质 IBL facts
标记为不可用；Forward shader 内只处理已经上传的 feature/material facts。

IBL 计算公式必须位于 `assets/shaders/glsl/common/ibl_lighting.glsl` 或等价
common shader。Forward pass 和 DeferredLighting 必须复用同一 common 入口，不得
各写一套公式。Skybox/background 直接渲染归 `REQ-073-f`，本 REQ 不改变
`SkyboxBackground` pass 执行顺序、开关或输出。Bloom 仍是 screen-space effect，本
REQ 只要求 Forward IBL 激活后开启 bloom 不产生错误图，不把 bloom 合并到物体
fragment draw。

### R6: Failure Isolation And Retry

失败不得污染当前 active IBL resources。

要求：

- 环境 bake 失败时，当前 environment IBL contribution 不启用；已有成功激活的旧
  IBL resources 保持不变。
- BRDF LUT bake 失败时，不关闭整个 scene；只让依赖该 LUT / BRDF model 的材质在
  Forward/Deferred shader-visible facts 中标记为 IBL unavailable。
- 写文件必须使用临时文件 / 临时 manifest，成功后原子切换，避免半成品被 loader
  识别。
- 每个失败都要给出修复方式，例如检查 URI、删除 invalid manifest、确认文件权限或
  重新运行 shader compile。
- 用户修复后可以再次执行 `bake ibl start`，新 job 从头开始 bake。

### R7: Deferred Structural Parity

Deferred SHALL 同步接入同一个 IBL shader / feature / resource contract。

要求：

- DeferredLighting graph source 声明和 Forward pass 使用同一组
  `feature.surfaceLighting` / `scene.environmentBake` / `scene.materialIblBake`
  事实；Deferred 可以有自己的 pass shape，但 IBL helper 和资源 ABI 不分叉。
- shader include 同一个 `common/ibl_lighting.glsl`。
- 本 REQ 不要求 Deferred 图像 smoke 通过；Deferred 验收是 shader 编译、reflection、
  graph/source/binding 结构一致。

### R8: Hard Cut Private Bake Path

完成态 default bake path SHALL NOT 调用 `IblBakeRenderer::bakeStaticEnvironment()`
作为 public/default shortcut。

旧路径处理：

- old `IblBakeRenderer` 可被替换为 graph executor 内部 implementation detail，但
  不能保留 public/default shortcut。
- positive tests 不得直接证明私有 renderer 成功。
- 保留的 legacy token 只能出现在 named negative audit 或历史说明中。

## 测试

- command/API tests: `bake ibl start` 返回 job id，status/logs/cancel 行为可见。
- job event tests: 多线程读取 event stream，sequence 增量稳定。
- cache tests: manifest unknown field、missing payload、wrong source hash、
  wrong mip layout、metadata-only payload 全部 rejected。
- command/API tests: `bake ibl start --force` 在无 running job 时忽略 valid cache；
  running job 存在时 rejected 或返回已有 job diagnostic。
- cache hit tests: valid cache 直接进入 `activate` phase，不跑 GPU bake，但仍触发
  completion callback。
- bake key tests: 同一 scene 中多个 `standard-pbr` object/material instance 只产生
  一个 material bake key；不同 environment URI/hash 产生不同 environment bake key。
- bake graph tests: `bake_environment_ibl.render-path.yaml` 和
  `bake_standard_pbr_brdf_lut.render-path.yaml` 能被 strict parser /
  RenderWorkCompiler 接受；缺 source/target/shader/format/payload 的变体 rejected。
- FrameGraphExecutor tests: bake graph work 由 `FrameGraphExecutor` 执行，默认 bake
  path 不绕过 RenderPathGraph / RenderWorkCompiler。
- bake output tests: `bake ibl start` 后 diffuse SH、prefiltered cubemap mip chain、
  environment manifest、standard-pbr material manifest、BRDF LUT 文件存在且格式、
  尺寸、mip count、SH coefficient count 符合预期。
- value sanity tests: SH YAML 有 9 组 RGB 且不能全零；prefiltered cubemap mip 尺寸
  由 resolution 递减；BRDF LUT 为 256 RG16Float KTX2，且不随每个 scene 重复生成。
- activation tests: bake 完成后不 reload scene，当前 `SceneResourceTable` 注册 live
  SH / cubemap / LUT payload，并通过两阶段提交刷新 active IBL generation。
- failure tests: 环境 bake 失败不替换 active resources；BRDF LUT 失败只跳过依赖该
  LUT 的 material IBL；失败日志包含 fix / retry。
- shader tests: Forward pass 和 DeferredLighting include
  `common/ibl_lighting.glsl`，reflection 看到同一套 IBL bindings。
- RenderWorkCompiler tests: graph 声明 `feature.surfaceLighting.enableIblLighting`
  但缺 `scene.environmentBake` 或 `scene.materialIblBake` 时 rejected；Forward
  feature 开关进入 shader-visible data。
- Bloom compatibility smoke: Forward IBL 激活后开启 bloom 不生成错误图；本 REQ 不
  改变 bloom 的 screen-space pass 归属。
- Vulkan smoke: Forward 场景执行 `bake ibl start`，不 reload scene，下一帧 render
  / debug dump 证明 PBR surface environment lighting 变化。
- rg audit:

```bash
rg -n "IblBakeRenderer|bakeStaticEnvironment|renderPath: IBLBake|ibl_prefilter_env|ibl_brdf_lut|HAS_IBL|EnvironmentUBO|iblIntensity" src assets docs notes
```

default positive path 不得依赖这些旧 token。

## 修改范围

- bake job service / command hooks
- RenderPathGraph resource schema for environment bake and Forward IBL inputs
- RenderWorkCompiler Forward IBL dependency diagnostics
- environment bake manifest loader/writer
- environment-adjacent bake asset writer
- reusable BRDF LUT asset writer
- SceneResourceTable environment IBL live resources
- `assets/shaders/glsl/common/ibl_lighting.glsl`
- Forward pass shader updates and `feature.surfaceLighting` graph/feature assets
- DeferredLighting shader/source contract updates
- bake render-path graph assets for environment IBL and standard-pbr BRDF LUT
- editor log / command prompt integration
- `FrameGraphExecutor` minimum abstraction and Vulkan bake graph execution tests

## 边界与约束

- environment HDR asset remains the source of radiance truth from
  `feature.environmentLighting`。
- bake assets are resources, not material parameters。
- material only carries shader-visible IBL support facts such as bake mask /
  BRDF model。
- RenderPathGraph owns bake pass execution shape and surface lighting feature
  selection; backend only executes the compiled contract through
  `FrameGraphExecutor`。
- pipeline is prepared before bake and not rebuilt after bake completes。
- Forward default realtime path must not add a separate IBL geometry/additive pass。
- Skybox direct rendering remains owned by `REQ-073-f`。

## 依赖

- `REQ-073-f`: environment map / skybox direct lighting。

## 后续工作

- `REQ-073-h`: reflection probe IBL extension。
- `REQ-073-i`: RenderFeature parameter architecture hard cut。

## 实施状态

未实施。

# REQ-073-h: Reflection Probe IBL Extension

> 2026-06-16 校准：旧 `REQ-073-h: IBL Lighting Post Effect` 已并入
> `REQ-073-g`。本 REQ 现在只处理 reflection probe：从场景节点出发捕获临时
> probe environment map，再复用 `REQ-073-g` 建立的环境 IBL bake、cache、
> activation 和 runtime lighting 合同。

## 背景

`REQ-073-g` 只烘焙 scene/global environment HDR 资产。Reflection probe 是这个
能力的局部扩展：它不是新的 lighting system，而是从 scene 中某个 probe 节点捕获
一个临时 EnvMap，再对这个 EnvMap 执行同一套 diffuse SH、prefiltered cubemap 和
BRDF LUT 资源流程。

拆到本 REQ 的原因是 probe 还需要 scene component、capture camera、influence volume
和 probe selection / blending policy。如果把它塞进 073g，会打断环境 HDR bake 的
基础闭环。

## 目标

1. 新增严格的 `ReflectionProbeComponent` scene contract。
2. 从 probe 节点生成 capture camera / cubemap face 任务。
3. 生成临时 probe EnvMap，并复用 `REQ-073-g` 的 bake 资产管线。
4. 把 probe bake manifest / payload 注册成 SceneResourceTable live resources。
5. 定义 Forward / Deferred 如何选择 global environment 与 local probe 贡献。
6. 保持 probe lighting 的 shader 公式复用 `common/ibl_lighting.glsl`。

## 非目标

- 不重新实现环境 HDR bake；复用 `REQ-073-g`。
- 不重新设计 Forward 主 lighting 形态；`REQ-073-g` 已要求 Forward 在现有 pass 内
  复用 common IBL helper。
- 不实现 DDGI、lightmap 或动态 every-frame probe update。
- 不解决 package 文件格式。

## 需求

### R1: Reflection Probe Scene Contract

scene YAML SHALL 支持严格的 `reflectionProbe` component。

最低字段：

```yaml
components:
  reflectionProbe:
    global: false
    capture:
      resolution: 256
      nearClip: 0.1
      farClip: 50.0
      includeSky: true
    influence:
      shape: sphere
      radius: 12.0
      blendDistance: 2.0
```

未知字段必须 fail-fast。Probe component 不是 ordinary CameraComponent，不能被
普通 view camera collection 消费。

### R2: Probe Capture RenderPathGraph

本 REQ SHALL 新增 graph-authored probe capture path。

要求：

- pass list、shader URI、sources、targets、resources、cubemap face iteration 都
  来自 graph。
- probe capture 输出临时 radiance cubemap EnvMap。
- capture path 可复用 073g 的 filter / bake / manifest 写入机制。
- 未声明 source/target 不得由 backend 临时补齐。

### R3: Probe Bake Cache

probe bake cache SHALL 保存 probe capture source 和 bake 输出。

要求：

- manifest 记录 probe node id / transform / influence / source scene hash /
  bake settings。
- payload 使用 073g 的 diffuse SH、prefiltered cubemap 和 BRDF LUT 事实模型。
- loader 只能注册 live texture / SH / LUT payload；metadata-only record 不得满足
  probe lighting。
- invalid cache fail-fast；missing cache 不隐式 capture，必须由 explicit bake job
  或 editor 操作触发。

### R4: Runtime Probe Selection

runtime SHALL 明确选择 global environment 与 local probe 的关系。

首版可以只支持：

- 一个 global environment bake。
- 零个或多个 local probes。
- 按 probe influence sphere 选择最近或最高权重 probe。
- 不做 box projection。

后续复杂 blending / parallax correction 应拆后续 REQ。

## 测试

- scene parser accepts/rejects `reflectionProbe` component。
- graph parser rejects undeclared capture source/target。
- probe capture expands cubemap face work deterministically。
- probe cache loader rejects missing face、wrong source hash、unknown field 和
  metadata-only payload。
- Vulkan tiny probe bake smoke captures a small scene, writes probe bake assets,
  and registers live SceneResourceTable resources。
- rg audit: private probe / IBL bake shortcuts do not satisfy the positive path。

## 修改范围

- scene component parser/saver
- probe capture graph assets
- RenderWorkCompiler cubemap face metadata
- probe bake cache manifest
- SceneResourceTable probe resources
- Forward / Deferred probe selection resources and diagnostics

## 依赖

- `REQ-073-g`: environment HDR async IBL bake and runtime lighting。

## 后续工作

- `REQ-073-i`: RenderFeature parameter architecture hard cut。

## 实施状态

未实施。

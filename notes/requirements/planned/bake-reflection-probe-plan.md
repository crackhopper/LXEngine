# PLAN: Reflection Probe Bake

> 2026-06-01 planned：本需求记录离线 reflection probe bake 的原理、数据流和实施计划。它不进入当前 active 实施队列，待 Offline Rendering Lab 的 ground truth renderer 稳定后再启动。

## 背景

离线 renderer 不只用于输出 camera ground truth 图，也可以生成实时 renderer 可消费的预计算资产。Reflection probe bake 的目标是在指定空间位置渲染六个方向的环境辐射，得到 cubemap，再供实时 IBL / 反射探针使用。

这类能力对应 Unreal Reflection Capture、Unity Reflection Probe，以及更高质量离线路径追踪生成的 probe 数据。LXEngine 当前已有实时 IBL 环境 bake 主线；本 planned REQ 的定位是未来把离线 path tracing 结果回灌到实时渲染。

## 原理

Reflection probe 可视为一个放置在场景中的全向相机：

1. 在 probe 位置构造 6 个 cubemap face camera。
2. 每个 face 覆盖 90 度 FOV。
3. 对每个 face 使用离线 renderer 渲染 scene-linear radiance。
4. 输出 cubemap face EXR 或引擎可读 cubemap asset。
5. 后续可按 roughness 生成 prefiltered mip chain。
6. 实时 renderer 在 shading 时根据反射方向采样该 cubemap。

关键约束：

- probe 结果是局部近似，不等同于完整全局光照。
- probe 位置、box/sphere influence、parallax correction 是实时消费端问题。
- 高 roughness 反射需要 prefilter，不应直接采样 sharp cubemap。

## 目标

1. 定义 reflection probe bake 的 profile schema。
2. 定义 cubemap face 渲染、输出和 metadata 约定。
3. 明确它如何复用 `SceneResourceTable`、`GpuScene`、path tracing integrator 和 EXR writer。
4. 明确未来如何回灌实时 IBL / reflection probe。

## 计划需求

### R1: Bake profile schema

未来在 `offlineRender.profiles` 中支持 probe bake profile。

示例：

```yaml
scene:
  offlineRender:
    profiles:
      reflection_probe_preview:
        backend: vulkan-compute
        integrator: path-tracing
        job: bake
        target:
          kind: reflectionProbe
          id: room_center_probe
          position: [0.0, 1.2, 0.0]
          resolution: 128
          samples: 64
          maxDepth: 4
          output: artifacts/offline/bakes/room_center_probe
```

### R2: Cubemap face camera generation

实现步骤：

1. 从 target position 生成 +X、-X、+Y、-Y、+Z、-Z 六个 view。
2. 每个 face 使用 90 度 FOV 和 1:1 aspect。
3. 固定 face orientation，避免 cubemap seam。
4. 每个 face 调用相同 offline render job，只替换 camera。

### R3: Output contract

首版输出建议：

```text
artifacts/offline/bakes/<probe-id>/
  px.exr
  nx.exr
  py.exr
  ny.exr
  pz.exr
  nz.exr
  probe.json
```

`probe.json` 至少记录：

- probe id
- source scene
- position
- resolution
- samples / maxDepth / seed
- face file list
- color space / EXR precision
- build id

### R4: Runtime 回灌计划

未来实时 renderer 消费 reflection probe 时需要：

- scene YAML 引用 bake result。
- runtime loader 能加载 cubemap face 或 packed cubemap asset。
- material/PBR shader 能选择 environment IBL 或 local reflection probe。
- 缺失 probe bake asset 时有诊断或 fallback。

示例：

```yaml
scene:
  reflectionProbes:
    - id: room_center_probe
      source: cache://bakes/room_center_probe/converted/probe.json
      influence:
        type: box
        min: [-3, 0, -3]
        max: [3, 3, 3]
```

## 修改范围计划

- `src/core/offline/` bake target 数据结构
- `src/infra/offline/` bake profile parser / job compiler
- `src/backend/vulkan/offline/` cubemap face render orchestration
- offline output writer
- future realtime probe loader / scene binding
- tests

## 依赖

- `REQ-053-a`
- `REQ-055-a`
- `REQ-075-c`
- 当前实时 IBL / PBR 资源消费模型

## 边界

- 本 planned REQ 不进入当前实现批次。
- 不要求 probe blending。
- 不要求 parallax correction。
- 不要求实时 editor UI。
- 不要求 Vulkan hardware RT。

## 实施状态

Planned，未进入当前 active 开发。

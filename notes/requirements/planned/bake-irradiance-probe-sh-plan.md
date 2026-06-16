# PLAN: Irradiance Probe / SH Bake

> 2026-06-01 planned：本需求记录 diffuse irradiance probe 与 SH bake 的原理、数据流和实施计划。它不进入当前 active 实施队列。

## 背景

Reflection probe 主要服务镜面/半镜面反射；diffuse 间接光需要低频 irradiance。常见做法是从环境或场景采样半球入射光，将结果存为 irradiance cubemap 或球谐系数 SH。

该能力未来可用于实时 diffuse IBL、局部 irradiance probe、以及和离线 path tracing ground truth 对比。

## 原理

Irradiance bake 的目标不是保存某个方向的 radiance，而是保存某个点处各法线方向可用的低频入射照明：

1. 在 probe 位置采样场景光照。
2. 对入射 radiance 进行 cosine-weighted convolution。
3. 将低频结果保存为 irradiance cubemap 或 SH 系数。
4. 实时 shader 根据 surface normal 查询 irradiance。

SH 路线通常存储更小，适合 diffuse；cubemap 路线更直观，便于调试。

## 目标

1. 定义 irradiance probe bake 的输入、输出和 metadata。
2. 明确 SH 与 irradiance cubemap 两种输出路线。
3. 明确它与 reflection probe 的差异，避免共用错误数据模型。

## 计划需求

### R1: Profile schema

示例：

```yaml
scene:
  offlineRender:
    profiles:
      irradiance_probe_preview:
        backend: vulkan-compute
        integrator: path-tracing
        job: bake
        target:
          kind: irradianceProbe
          id: room_center_irradiance
          position: [0.0, 1.2, 0.0]
          outputMode: sh3
          samples: 256
```

### R2: SH output

实现步骤：

1. 对 probe 位置采样球面方向。
2. 对每个方向获得 radiance。
3. 乘以 diffuse convolution 权重。
4. 投影到 SH basis。
5. 输出 RGB SH coefficients。

metadata 至少记录：

- SH order
- basis convention
- coefficient order
- color space
- sample count / seed

### R3: Irradiance cubemap output

实现步骤：

1. 生成低分辨率 cubemap。
2. 每个 texel 对应一个法线方向。
3. 对该方向半球采样，积分 irradiance。
4. 输出 EXR face 或 packed cubemap。

### R4: Runtime 回灌计划

未来实时 renderer 消费 irradiance probe 时需要：

- scene 引用 SH 或 irradiance cubemap。
- shader 根据 normal 查询 diffuse irradiance。
- 多 probe blending 另行规划，不放入首版。

## 修改范围计划

- offline bake target schema
- SH math utilities
- irradiance convolution shader / compute pass
- output metadata
- future realtime diffuse IBL/probe consumer
- tests

## 依赖

- `REQ-075-c`
- Reflection Probe Bake planned route

## 边界

- 本 planned REQ 不进入当前实现批次。
- 不要求多 probe blending。
- 不要求 DDGI / dynamic probe update。
- 不要求 lightmap。

## 实施状态

Planned，未进入当前 active 开发。

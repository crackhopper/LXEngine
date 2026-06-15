# PLAN: Lightmap Bake

> 2026-06-01 planned：本需求记录 lightmap bake 的原理、数据流和实施计划。它复杂度高，不进入当前 active 实施队列。

## 背景

Lightmap bake 将静态场景表面的间接光或完整静态光照预计算到纹理中，实时渲染时通过 UV2 采样。它能显著降低实时成本，但涉及 unwrap、chart packing、texel visibility、漏光处理、材质分离和 scene binding，复杂度明显高于 probe bake。

因此 lightmap 不应塞进 reflection probe MVP，而应单独 planned。

## 原理

Lightmap bake 的基本流程：

1. 为静态 mesh 准备 lightmap UV，也叫 UV2。
2. 把多个 mesh/chart 打包到 lightmap atlas。
3. 对 atlas 中每个有效 texel 反查世界空间 surface point。
4. 从该点沿半球采样直接光和间接光。
5. 写入 lightmap texel。
6. 实时 renderer 在材质 shading 时采样 lightmap，并与动态光/IBL 合成。

关键难点：

- UV chart 边界 padding。
- texel 到 surface 的稳定映射。
- backface / visibility / self-intersection。
- 多材质 mesh 的 atlas 分配。
- direct-only、indirect-only、combined lightmap 的语义。

## 目标

1. 明确 lightmap bake 的数据依赖和实施阶段。
2. 避免在 probe bake 阶段引入 lightmap 复杂度。
3. 为未来静态 GI / baked lighting 建立路线。

## 计划需求

### R1: Mesh UV2 requirement

首版 lightmap bake 应要求 mesh 具备 UV2。

计划：

1. loader 暴露 UV2。
2. scene compiler 标记 mesh 是否 lightmap-ready。
3. 缺 UV2 时给出诊断。
4. atlas generator 作为后续增强，不作为首版前置。

### R2: Atlas and chart contract

计划定义：

- lightmap atlas id
- chart id
- texel padding
- mesh instance 到 atlas rect 的映射
- material slot 与 atlas chart 的关系

### R3: Texel shading

实现步骤：

1. 遍历 atlas 有效 texel。
2. 由 UV2 反查 triangle barycentric。
3. 计算 world position / normal。
4. 发射 shadow ray / indirect path。
5. 写入 irradiance 或 radiance。

### R4: Output and scene binding

输出建议：

```text
artifacts/offline/bakes/lightmaps/<scene-name>/
  lightmap_000.exr
  lightmap_001.exr
  lightmaps.json
```

scene 未来引用：

```yaml
scene:
  lightmaps:
    - id: static_gi_0
      source: cache://bakes/static_gi_0/converted/lightmaps.json
```

### R5: Runtime composition

未来实时 renderer 需要明确：

- lightmap 是 direct、indirect 还是 combined。
- 是否与 realtime direct light 相加。
- 是否参与 tone mapping 前的 scene-linear lighting。
- 是否支持 material AO 或 emissive。

## 修改范围计划

- mesh loader UV2 support audit
- lightmap atlas/chart 数据结构
- offline texel bake integrator
- lightmap output writer
- scene/runtime lightmap binding
- realtime shader lightmap sampling
- tests

## 依赖

- `REQ-076-g`
- Irradiance Probe / SH Bake planned route
- mesh UV2 / asset pipeline readiness

## 边界

- 本 planned REQ 不进入当前实现批次。
- 不要求自动 unwrap 作为首版。
- 不要求 progressive editor preview。
- 不要求动态物体 light probe。
- 不要求 production-grade light leak fixing。

## 实施状态

Planned，未进入当前 active 开发。

# 光源：绑定到节点的 scene-level 光照数据

光源像挂在舞台上的灯具。节点决定灯具在场景里的身份和位置，light 对象决定颜色、入射照度倍率、方向、范围和它参加哪些 pass。当前第一盏可用 directional light 仍承担 CSM 主光源角色。

## 当前光源对象

| 类型 | 当前状态 | 数据去向 |
|---|---|---|
| `DirectionalLight` | 可创建、可 attach 到节点、可提供独立 `LightUBO` | realtime Forward / Deferred / Shadow 当前实际消费 |
| `PointLight` | 可创建、可保存基础参数，当前不直接返回独立 UBO | 汇总进 `SceneLightsUBO`，主 PBR shader 尚未遍历 |
| `SpotLight` | 可创建、可保存方向/范围/锥角，当前不直接返回独立 UBO | 汇总进 `SceneLightsUBO`，主 PBR shader 尚未遍历 |
| `SceneLightsData` | 场景级聚合 UBO，binding name 为 `SceneLightsUBO` | C++ / GLSL layout 已有；具体 shader 需要显式读取 |

材质文件里的 `resources` 不负责声明 `SceneLightsUBO`。这是系统注入的 scene-level 资源，材质系统会根据 shader 反射和系统绑定规则处理它。

## 当前渲染边界

| 路径 | 当前光源支持 |
|---|---|
| Realtime Forward PBR | 直接光读取 `LightUBO` 单 directional light；IBL 另走 environment resources |
| Realtime Deferred lighting | fullscreen lighting 当前同样读取 `LightUBO` 单 directional light |
| Shadow / CSM | directional light 提供 `shadowViewProj`、cascade matrices 和 shadow params |
| `SceneLightsUBO` | C++ 会按 directional / point / spot 三数组填充，供后续多光源 shader 使用 |
| Offline ray tracer | storage 构建阶段只取第一盏 directional light，写入 `SceneFrameParams` |

因此，Point/Spot 当前是 editor/scene/runtime 数据链路的一等对象，但还不是主 PBR/Deferred/offline 直接光照闭环的一等输入。

## YAML 到 light

```yaml
light:                         # -> LightNodeState
  kind: Directional             # -> DirectionalLight
  direction: [-0.3, -1.0, -0.5]
  color: [1.0, 0.98, 0.9]
  intensity: 1.0          # 当前字段名；按 irradiance scale 理解
  shadowStrength: 0.7          # -> DirectionalLight::setShadowStrength()
  shadowDistance: 80.0         # -> DirectionalLight::setShadowDistance()
  shadowCascadeCount: 4        # -> DirectionalLight::setShadowCascadeCount()
```

运行时装配时，`SceneRuntime` 会创建对应 light，并通过 `Scene::attachLight(node, light)` 绑定到节点。

`intensity` 是历史字段名。当前文档按入射照度/irradiance 倍率理解它：directional light 的 `color * intensity` 直接进入 direct lighting；Point/Spot 后续进入 shader loop 时，应结合距离、range 和 cone 衰减，计算当前 shading point 的 incident irradiance。

## Directional shadow 数据

| 字段 | 当前作用 |
|---|---|
| `DirectionalLightData.shadowViewProj` | 当前 shadow pass 的 light-space 矩阵 |
| `DirectionalLightData.cascadeViewProj[4]` | Forward shader 采样 CSM 时使用的四个矩阵 |
| `DirectionalLightData.cascadeSplits` | 相机 view-space 的 cascade 切分点 |
| `DirectionalLightData.shadowParams` | shadow map size、bias、strength、cascade count |

## 继续阅读

- [文档到 Runtime](document-runtime-flow.md)
- [材质系统](../concepts/material/index.md)
- [light.hpp](/home/lixiang/proj/LXEngine/src/core/scene/light.hpp:1)
- [SceneLightsUBO 与 shader 边界](../tutorial/custom-light/04-scene-lights-shader-boundary.md)

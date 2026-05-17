# 光源：绑定到节点的 scene-level 光照数据

光源像挂在舞台上的灯具。节点决定灯具在场景里的身份和位置，light 对象决定颜色、强度、方向、范围和它参加哪些 pass。v0.1.1 中，第一盏 directional light 还承担 CSM 主光源角色。

## 当前光源对象

| 类型 | 当前状态 | 数据去向 |
|---|---|---|
| `DirectionalLight` | 可创建、可 attach 到节点、可提供独立 `LightUBO` | scene-level resource |
| `PointLight` | 可创建、可保存基础参数，当前不直接返回独立 UBO | 汇总进 `SceneLightsUBO` 方向 |
| `SpotLight` | 可创建、可保存方向/范围/锥角，当前不直接返回独立 UBO | 汇总进 `SceneLightsUBO` 方向 |
| `SceneLightsData` | 场景级聚合 UBO，binding name 为 `SceneLightsUBO` | shader 系统资源 |

材质文件里的 `resources` 不负责声明 `SceneLightsUBO`。这是系统注入的 scene-level 资源，材质系统会根据 shader 反射和系统绑定规则处理它。

## YAML 到 light

```yaml
light:                         # -> LightNodeState
  kind: Directional             # -> DirectionalLight
  direction: [-0.3, -1.0, -0.5]
  color: [1.0, 0.98, 0.9]
  intensity: 1.0
  shadowStrength: 0.7          # -> DirectionalLight::setShadowStrength()
  shadowDistance: 80.0         # -> DirectionalLight::setShadowDistance()
  shadowCascadeCount: 4        # -> DirectionalLight::setShadowCascadeCount()
```

运行时装配时，`SceneRuntime` 会创建对应 light，并通过 `Scene::attachLight(node, light)` 绑定到节点。

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

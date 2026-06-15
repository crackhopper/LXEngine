# SceneLightsUBO 与 shader 边界：数据已经有，光照 loop 还要接

这一章专门拆开一个容易误解的点：当前场景系统已经能收集 Directional / Point / Spot 三类 light，并生成 `SceneLightsUBO`；但当前 realtime PBR、Deferred lighting 和 offline ray tracer 的直接光照并没有完整遍历这组三类 light。

## 当前 C++ 侧怎样收集多光源

`Scene::getSceneLevelResources(...)` 会按 pass 收集 scene-level resource。对 light 来说有两条路径：

| 路径 | 当前作用 |
|---|---|
| `LightBase::getUBO()` | `DirectionalLight` 返回独立 `LightUBO`，Forward / Deferred / Shadow 当前实际消费它 |
| `SceneResourceTable::buildSceneLightsUboResource(...)` | 把 Directional / Point / Spot 汇总进 `SceneLightsUBO` |

`SceneLightsUBO` 当前是固定三数组布局：

| 数组 | 上限 | 主要字段 |
|---|---:|---|
| directional | 4 | `direction`、`colorIntensity` |
| point | 16 | `positionRange`、`colorIntensity` |
| spot | 8 | `positionRange`、`directionCone`、`colorIntensity` |

`counts.x/y/z` 分别记录 directional、point、spot 数量。C++ 和 GLSL 结构要保持一致，否则 shader 读到的字段会错位。

## 当前 shader 实际消费什么

| 路径 | 当前 shader 输入 | 真实状态 |
|---|---|---|
| Forward PBR | `LightUBO` | 使用单个 directional light 计算直接光；IBL 另走 environment resources |
| Deferred lighting | `LightUBO` | fullscreen lighting 当前同样按单 directional light 计算 |
| Shadow / CSM | `LightUBO.shadowViewProj`、cascade 数据 | directional light 是当前 shadow 主光源 |
| Offline ray tracer | `SceneFrameParams.lightDirectionIntensity`、`lightColorEnvironment` | storage 侧只取第一盏 directional light |
| `SceneLightsUBO` include | `scene_lights_ubo.glsl` | GLSL layout 已存在，但当前主 PBR/Deferred shader 没有 include 和遍历它 |

所以验证 point/spot 时，能看到创建、保存、Inspector、debug helper、scene-level 数据，并不等于能在当前 PBR shader 中看到 point/spot direct lighting。要让它们真正照亮表面，需要让具体 shader 读取 `SceneLightsUBO` 或新的 light buffer，并实现 per-light evaluate loop。

## irradiance 语义

当前字段名仍叫 `intensity`，但直接光 shader 更适合把它理解成 incident irradiance scale：

| 类型 | 建议语义 |
|---|---|
| Directional | `color * intensity` 直接表示来自该方向的入射照度 |
| Point | shader 根据位置、距离、range 衰减，把 `color * intensity` 转成当前点的入射照度 |
| Spot | 在 point 衰减基础上叠加方向 cone 衰减，得到当前点的入射照度 |

这样 Forward、Deferred 和 Offline 后续可以共享同一套 light evaluate 语义，而不是把同一个字段在不同 shader 里解释成不同单位。

## 统一 light record 的后续方向

如果要配合 bindless / indirect draw 的资源模型，多光源数据可以从三数组 UBO 演进成一组统一 record。核心不是“所有灯物理上一样”，而是 shader ABI 可以统一：

```glsl
struct SceneLightRecord {
    vec4 positionRange;      // xyz: position, w: range
    vec4 directionCone;      // xyz: direction, w: cone or packed angle
    vec4 colorIrradiance;    // rgb: color, w: irradiance scale
    uvec4 meta;              // x: type, y: flags, z: shadow/probe index
};
```

Directional / Point / Spot 仍然由不同 evaluate 函数处理。统一 record 的价值是让 scene-level light buffer、shader loop、debug dump 和 offline storage 更容易对齐。

## Area light 暂不进入当前 light 教程

面光源后续不要急着当成第四种普通 light 塞进当前 UBO。它可能更接近 emissive geometry、lightmap、light probe、IBL importance sampling 或专门的 lighting asset。等 PBR + IBL 的测试闭环全部验收后，再决定 area light 属于 direct light、baked lighting，还是 probe / environment 资产链路。

## 我们已经学会了什么

我们把当前 light 支持拆成三层：editor/scene 能处理三类 light，C++ 能生成三类 light 的聚合数据，但主渲染 shader 仍需要后续工作才能完整消费多光源。

## 下一步

进入 [05 在 editor 中验证](05-verify-and-debug-lights.md)，用这个边界去判断画面现象。

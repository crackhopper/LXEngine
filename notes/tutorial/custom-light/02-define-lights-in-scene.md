# 在场景里定义光源：给舞台摆灯

定义光源像给舞台摆灯：我们先选灯具类型，再放位置、调颜色、设亮度，最后把舞台布置保存成 scene 文档。当前 `lxe_editor` 的 light 节点就是沿着这条思路工作。

## 当前 scene 文档里的 light

`SceneNodeDocument` 里有一个可选的 `light` 状态。它让普通 scene node 也能成为光源节点。

| 字段 | 含义 | 类比 |
|---|---|---|
| `kind` | `Directional` / `Point` / `Spot` | 选择灯具型号 |
| `color` | RGB 颜色 | 灯片颜色 |
| `intensity` | 光照强度 | 调光台推杆 |
| `range` | 点光源和聚光灯影响距离 | 灯能照到多远 |
| `direction` | 平行光和聚光灯方向 | 灯头朝向 |
| `innerConeDegrees` / `outerConeDegrees` | 聚光灯内外锥角 | 光束中心和边缘 |

## 一个最小 light YAML 片段

下面的形状是教程里的概念示例，用来帮助我们读懂 scene document。实际字段名应以当前保存出来的 scene 文件和 `scene_document.hpp` 为准。

```yaml
nodes:
  - name: WarmSpot
    transform:                         # -> SceneNodeDocument.transform
      translation: [0.0, 3.0, 2.5]
    light:                             # -> SceneNodeDocument.light
      kind: Spot                       # -> LightKind::Spot
      color: [1.0, 0.92, 0.72]         # -> LightNodeState.color
      intensity: 4.0                   # -> LightNodeState.intensity
      range: 8.0                       # -> LightNodeState.range
      direction: [0.0, -0.7, -0.7]     # -> LightNodeState.direction
      innerConeDegrees: 18.0           # -> SpotLight.innerConeCos
      outerConeDegrees: 34.0           # -> SpotLight.outerConeCos
```

这里的关键不是背字段，而是看懂映射关系：YAML 是记录单，`LightNodeState` 是内存里的记录，`SpotLight` 是渲染真正使用的灯具。

## 通过 command 创建和修改 light

当前 command surface 已经包含三类 light 的创建与字段编辑能力。教程里推荐先走 command，再观察 Inspector 和保存文件。

| 动作 | 当前命令意图 | 我们检查什么 |
|---|---|---|
| 创建平行光 | `light:directional` | 场景中出现 directional light 节点 |
| 创建点光源 | `light:point` | 节点位置影响照明中心 |
| 创建聚光灯 | `light:spot` | cone 参数影响光束范围 |
| 修改字段 | `light.color` / `light.intensity` 等 | Inspector 与保存后的 scene 同步 |

命令名和参数以当前 command completion 为准。需要确认时，先看 `src/core/editor/commands/builtin_commands.cpp`，那里是当前权威入口。

## 我们已经学会了什么

我们知道 light 在 scene 里不是孤立对象，而是 scene node 上的一组可选状态。创建、编辑、保存、加载都围绕这组状态流动。

## 下一步

进入 [03 C++ 扩展光源能力](03-extend-light-in-cpp.md)，看新增 light kind 为什么目前仍然是多模块改造。

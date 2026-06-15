# 光源：三类内置灯和 shader 边界

灯光系统像一套舞台灯：当前仓库已经有平行光、点光源、聚光灯这三类内置灯，也有把场景 light 汇总成 GPU 数据的 `SceneLightsUBO` 通道。这个系列只讲当前能从代码里验证的链路：怎么创建和保存 light，C++ 怎样收集它们，shader 当前真正消费了哪些数据。

## 先把“能创建”和“能照亮”分开

`lxe_editor` 当前可以创建、编辑、保存、加载 Directional / Point / Spot 三类 light。`SceneResourceTable` 也会把这三类 light 收集进 `SceneLightsUBO`。但实时 PBR/Deferred 的直接光照 shader 仍主要读取单个 directional `LightUBO`；offline ray tracer 也只取第一盏 directional light。教程会明确区分这些层级，避免把“数据已经收集”误读成“所有 shader 已经做多光源照明”。

## 光源链路中的核心对象

| 环节 | 当前对象 | 作用 |
|---|---|---|
| 场景记录 | `SceneNodeDocument.light` | 保存 light kind 和参数 |
| 运行时对象 | `DirectionalLight` / `PointLight` / `SpotLight` | editor runtime 和 core scene 使用的 light 类型 |
| GPU 聚合 | `SceneLightsData` | 把三类 light 整理成 `directional` / `point` / `spot` 三组数组 |
| 当前直接光 | `LightUBO` | Forward / Deferred / Shadow 当前实际读取的 directional light 合同 |
| 作者入口 | CommandBus / Inspector | 创建和修改 light 节点 |

## 当前可实践章节

| 章节 | 我们学什么 |
|---|---|
| [01 当前光源积木](01-current-light-building-blocks.md) | 三类 light 在代码和场景里如何表示 |
| [02 在场景里定义光源](02-define-lights-in-scene.md) | 用 scene document 与 command 创建、保存光源 |
| [03 C++ 扩展光源能力](03-extend-light-in-cpp.md) | 当前手工扩展要经过哪些模块 |
| [05 在 editor 中验证](05-verify-and-debug-lights.md) | Inspector、debug helper、shader 现象如何一起检查 |

## 当前真实边界

| 路径 | 当前状态 | 说明 |
|---|---|---|
| 创建/保存三类 light | 可用 | `LightKind::Directional`、`Point`、`Spot` 已在 scene document、command、runtime 和 Inspector 里接通 |
| 收集三类 light 到 GPU 数据 | 可用 | `SceneLightsUBO` 当前是三组数组：directional 4、point 16、spot 8 |
| Realtime PBR 多光源直接照明 | 未完成 | 当前 PBR/Deferred shader 仍读取 `LightUBO` 单 directional 主光 |
| Offline 多光源 | 未完成 | 当前 offline storage 和 compute shader 只取第一盏 directional light |
| 新增完全不同的 light kind | 手工改造 | 需要同步 scene、runtime、command、Inspector、debug draw 和 shader ABI |

`intensity` 这个字段目前保留旧命名。教程里按直接光的入射照度/irradiance 倍率理解它：directional light 是整场景的方向入射照度；point/spot 若进入 shader loop，也应先在 shader 中结合距离、range 和 cone 算出当前 shading point 的 incident irradiance。

## 完成后我们能判断什么

这个系列会把“灯光是什么”和“灯光怎样进入渲染”拆开讲。前者是作者体验，后者是 engine 合同。

## 下一步

进入 [01 当前光源积木](01-current-light-building-blocks.md)。

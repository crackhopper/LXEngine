# 当前光源积木：三种舞台灯

我们先把光源看成舞台上的三种灯具：太阳灯负责给整个舞台一个方向，裸灯泡从一个点向四周照亮，手电筒只照一个锥形区域。LXEngine 当前把它们分别叫做 `DirectionalLight`、`PointLight` 和 `SpotLight`。

## 三种灯具先解决三类照明问题

| 灯具 | 当前类型 | 类比 | 主要参数 |
|---|---|---|---|
| 平行光 | `DirectionalLight` | 远处的太阳 | `direction`、`color`、`intensity` |
| 点光源 | `PointLight` | 房间里的灯泡 | `position`、`color`、`intensity`、`range` |
| 聚光灯 | `SpotLight` | 舞台追光灯 | `position`、`direction`、`range`、`innerConeCos`、`outerConeCos` |

这个类比帮助我们分清两件事：灯的“形状”决定它照亮空间的方式，灯的“参数”决定它照得多亮、什么颜色、影响多远。

## 光源数据在仓库里的连接点

| 文件 | 作用 |
|---|---|
| `src/core/scene/light.hpp` | 定义三类 light、`SceneLightsData` 与 GPU UBO 结构 |
| `assets/shaders/glsl/scene_lights_ubo.glsl` | GLSL 侧读取光源数组的合同 |
| `src/demos/lxe_editor/scene_document.hpp` | editor scene 文档里的 `LightKind` 和 `LightNodeState` |
| `src/demos/lxe_editor/scene_runtime.cpp` | 把 scene 文档转换成运行时 light 数据 |
| `src/core/editor/commands/builtin_commands.cpp` | command bus 创建与修改 light 的入口 |

`light.hpp` 是灯具的“零件定义”，`scene_document.hpp` 是舞台记录单，`scene_runtime.cpp` 是把记录单搬上舞台的工作人员，shader UBO 是舞台灯控台和渲染程序之间的插座。

## 数据如何走到 shader

| 阶段 | 当前对象 | 说明 |
|---|---|---|
| 作者输入 | `SceneNodeDocument.light` | 记录 light kind、颜色、强度、range、方向等字段 |
| 运行时收集 | `SceneLightsData` | 把场景里的 light 按类型整理成数组 |
| GPU 合同 | `SceneLightsUBO` | 固定布局传给 shader |
| Shader 读取 | `scene_lights_ubo.glsl` | GLSL 按同一布局读取 light 数组 |

我们在任何一层改字段，都要考虑后面几层是否理解这个字段。光源这条链路最容易出错的地方，不是单个参数写错，而是 C++、scene YAML、editor、shader 四边没有对齐。

## 当前边界

当前底座对三类内置光源比较明确，但“新增一种完全不同的 light kind”还没有稳定注册入口。新增类型时需要同时修改 light 数据、scene document、runtime 收集、command 补全、Inspector、debug draw 和 shader 逻辑。

这个边界会在 [REQ-042-a](../../requirements/pending/042-a-tutorial-light-asset-and-custom-light-registry.md) 中被整理成可教学的注册表模型。

## 我们已经学会了什么

我们把光源拆成了三层：作者看到的 light 节点、运行时收集的 light 数据、shader 读取的 UBO 合同。

## 下一步

进入 [02 在场景里定义光源](02-define-lights-in-scene.md)，从 scene document 角度创建和保存 light。

# 自定义灯光：先认识舞台灯，再设计新的灯具

灯光系统像一套舞台灯：当前仓库已经有平行光、点光源、聚光灯这三类常用灯具，也有把灯具参数送进 shader 的通道。这个系列先教我们使用现有灯具，再解释未来如何把“新的灯具类型”做成可注册、可保存、可调试的扩展点。

## 当前三类灯已经够用，为什么还要谈 custom light

内置三类灯能覆盖很多基础场景，但教程要讲“扩展灯光”时，会立刻遇到重复劳动：新增 light kind 不只是加一个 C++ struct，还要同步 scene document、runtime 收集、Inspector、command、debug helper 和 shader 合同。这个系列先让我们看懂当前三类灯怎么走完整链路，再把重复劳动收束成未来 registry 需求。

## 光源链路中的核心对象

| 环节 | 当前对象 | 作用 |
|---|---|---|
| 场景记录 | `SceneNodeDocument.light` | 保存 light kind 和参数 |
| 运行时收集 | `SceneLightsData` | 把场景 light 整理成 renderer 使用的数据 |
| GPU 合同 | `SceneLightsUBO` / `scene_lights_ubo.glsl` | 让 C++ 与 GLSL 读取同一份布局 |
| 作者入口 | CommandBus / Inspector | 创建和修改 light 节点 |

## 当前可实践章节

| 章节 | 我们学什么 |
|---|---|
| [01 当前光源积木](01-current-light-building-blocks.md) | 三类 light 在代码和场景里如何表示 |
| [02 在场景里定义光源](02-define-lights-in-scene.md) | 用 scene document 与 command 创建、保存光源 |
| [03 C++ 扩展光源能力](03-extend-light-in-cpp.md) | 当前手工扩展要经过哪些模块 |
| [04 光源资产与注册表](04-light-assets-and-registry.md) | 未来更适合教学和复用的 light asset 路径 |
| [05 在 editor 中验证](05-verify-and-debug-lights.md) | Inspector、debug helper、shader 现象如何一起检查 |

## 未来 registry 章节为什么放在最后

| 路径 | 状态 | 说明 |
|---|---|---|
| 使用 Directional / Point / Spot | 当前可用 | `SceneNodeDocument.light.kind`、command bus 与 runtime 都能处理 |
| 新增 C++ light kind | 当前可讲原理 | 需要同步修改多处代码，不是稳定扩展 API |
| light preset / custom light registry | 未来能力 | 由 [REQ-042-a](../../requirements/pending/042-a-tutorial-light-asset-and-custom-light-registry.md) 跟踪 |

我们先学习当前三类 light，是为了知道 registry 要解决什么痛点。否则未来 YAML 看起来会像凭空出现的配置格式，而不是对当前手工链路的整理。

## 完成后我们能判断什么

这个系列会把“灯光是什么”和“灯光怎样进入渲染”拆开讲。前者是作者体验，后者是 engine 合同。

## 下一步

进入 [01 当前光源积木](01-current-light-building-blocks.md)。

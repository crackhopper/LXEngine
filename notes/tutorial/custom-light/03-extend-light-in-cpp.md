# C++ 扩展光源能力：新增一种灯具要改哪些齿轮

新增 light kind 像给舞台系统接入一种新灯具。灯本身只是一个零件，真正麻烦的是灯控台、布线图、调试标记和演出程序都要认识它。当前仓库可以解释这条链路，但还没有把它收束成稳定插件接口。

## 当前手工路径

| 步骤 | 需要触达的区域 | 原因 |
|---|---|---|
| 定义数据 | `src/core/scene/light.hpp` | C++ 侧要有新 light 的参数结构 |
| 扩展文档 | `scene_document.hpp` / scene runtime loader | scene 文件要能保存和读取 kind 与字段 |
| 收集运行时数据 | `scene_runtime.cpp` | editor scene 要转换成 renderer 使用的数据 |
| 扩展命令 | `builtin_commands.cpp` | command bus 要能创建和修改新字段 |
| 扩展 Inspector | editor UI 相关代码 | 作者要能看到和编辑字段 |
| 扩展 debug draw | editor helper 相关代码 | 视口里要能看懂 light 范围 |
| 扩展 shader 合同 | `scene_lights_ubo.glsl`、`LightUBO` 或新的 light buffer | 渲染公式要能使用新数据 |

这张表就是“新增 light kind”的真实成本。教程可以带我们理解它，但不能把这条手工路径说成轻量扩展点。

## 一个假想的 TubeLight 为什么不该急着进当前教程

我们用 `TubeLight` 做教学例子：它像一根发光灯管，需要两个端点、颜色、强度和影响半径。

| 参数 | 含义 | 对应现有概念 |
|---|---|---|
| `start` | 灯管起点 | 类似 point light 的位置 |
| `end` | 灯管终点 | 新增形状信息 |
| `color` | 颜色 | 与现有 light 一致 |
| `intensity` | 当前字段名；语义应收敛到 irradiance scale | 与现有 light 一致 |
| `range` | 影响距离 | 与 point / spot 类似 |

如果今天手工实现它，第一步会是在 C++ light 数据里表达这些字段。随后每个使用 light 数据的层都要回答同一个问题：`TubeLight` 应该怎样保存、怎样编辑、怎样画 debug helper、怎样传给 shader。

当前教程不把 `TubeLight` 当作下一步要求。它只是帮助我们看清楚新增 light kind 的成本。当前真正需要先补齐的是：Point / Spot 已经能创建和收集，但 realtime PBR/Deferred/offline 还没有完整消费多光源直接照明。

## 多光源 ABI 更适合怎样演进

当前 `SceneLightsUBO` 是三组数组：directional、point、spot 分开存。考虑到项目已经有 bindless / indirect draw 方向，后续多光源 shader ABI 可以考虑收敛成一组统一 `LightRecord`，用 `type` 区分三类 light：

| 字段 | Directional | Point | Spot |
|---|---|---|---|
| `positionRange.xyz/w` | 可空 / 未用 | world position / range | world position / range |
| `directionCone.xyz/w` | direction / 未用 | 可空 / 未用 | direction / outer cone |
| `colorIrradiance.rgb/w` | color / irradiance | color / irradiance scale | color / irradiance scale |
| `meta` | type、flags、shadow index | type、flags | type、flags、cone packing |

这样 shader loop 可以遍历同一组 light record，再按 `type` 调用 directional、point、spot 的 evaluate 函数。bindless / indirect draw 本身不自动带来多光源 ABI，但它让“draw 侧资源索引”和“scene-level light buffer”更适合保持稳定、统一、批量友好的结构。

## Area light 先不要混进当前 direct light

面光源不是当前三类 direct light 的自然延伸。等 IBL 测试全部验收后，它更可能和 emissive geometry、lightmap、light probe、environment importance sampling 这类资产一起设计，而不是急着塞进当前 point/spot/directional 的 UBO。

## 我们已经学会了什么

我们看清了当前新增 light kind 的真实链路：数据、文档、运行时、命令、Inspector、debug draw、shader 都要对齐。

## 下一步

进入 [05 在 editor 中验证](05-verify-and-debug-lights.md)，用 Inspector、debug helper 和 shader 现象一起检查。

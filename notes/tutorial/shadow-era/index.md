# Shadow 阶段：从一张场景看懂多 Pass

Shadow 教程像在舞台上摆一盏主灯、一块地板和一个会投影的道具。我们先打开固定场景，确认哪些对象负责接收阴影、哪些对象负责投射阴影，再把画面背后的 `Shadow` pass、`Forward` pass 和 CSM 资源连接起来。

## 这一组教程解决什么

0.2.0-pre 的渲染主线已经从单一 forward draw 进入 graph-driven shadow/forward 路径：同一个 renderable 可以先参加 depth-only shadow pass，再参加 forward pass。教程不要求我们先理解完整 Vulkan 细节，而是从可打开的 scene 开始，把每个运行时对象和文档字段对应起来。

| 教程资产 | 作用 | 当前文件 |
|---|---|---|
| 固定相机 | 让阴影观察角度稳定 | `assets/scenes/shadow_tutorial.scene.yaml` |
| `ground_receiver` | 接收阴影的地面 | `builtin://lxe_editor/primitives/plane` |
| `cube_caster` | 投射阴影的物体 | `builtin://lxe_editor/primitives/cube` |
| `dir_light` | 当前主 directional shadow light | `light.kind: Directional` |

## 阅读顺序

1. [打开 Shadow 教程场景](01-open-shadow-scene.md)：确认 scene 能被 editor 加载、保存和重载。
2. [Shadow pass 怎样写资源](02-shadow-pass-flow.md)：理解 `Pass_Shadow` 为什么先写 depth。
3. [Forward pass 怎样读 CSM](03-csm-reading-path.md)：理解四个 cascade 如何变成 shader binding。
4. [调节阴影时先看哪些边界](04-shadow-tuning-and-limits.md)：理解当前 directional shadow 可调入口和边界。

## 当前能力和后续扩展分开看

| 路径 | 当前状态 | 说明 |
|---|---|---|
| directional light 作为 CSM 主光源 | 当前可用 | shadow pass 和 Forward pass 都围绕 directional `LightUBO` / cascade 数据工作 |
| shadow strength / distance / cascade count | 当前可通过 scene YAML、command、Inspector 调整 | 字段保存在 light state，并回写到 scene |
| point / spot shadow | 当前未完成 | Point/Spot 可创建和保存，但没有对应 shadow map 管线 |
| 多光源 shadow | 当前未完成 | 需要先完成 shader 多光源直接光照和 light buffer 语义，再设计 shadow/probe 索引 |

## 下一步

进入 [01 打开 Shadow 教程场景](01-open-shadow-scene.md)。

# 调节阴影时先看哪些边界

调阴影像调舞台灯：我们能先改灯光强度、照射距离和分段数量，但不能把整套灯具系统临时变成插件市场。当前教程只使用已经落地的 directional CSM 入口。

## 当前可调入口

Directional light 的 shadow 参数有三条作者表面：scene YAML、CommandBus、Inspector。它们最终都调用同一组 `DirectionalLight` setter。

| 参数 | YAML 字段 | Command field | 当前作用 |
|---|---|---|---|
| shadow strength | `light.shadowStrength` | `light.shadowStrength` | 控制 forward shader 中阴影混合强度 |
| shadow distance | `light.shadowDistance` | `light.shadowDistance` | 控制 CSM 覆盖的最远相机深度 |
| cascade count | `light.shadowCascadeCount` | `light.shadowCascadeCount` | 控制 active cascade 数量，范围 1–4 |

Command 例子：

```text
set /dir_light.shadowStrength 0.8
set /dir_light.shadowDistance 120
set /dir_light.shadowCascadeCount 4
```

Inspector 中选择 directional light 节点后，可以在 Light 区域调整 `Shadow Strength`、`Shadow Distance` 和 `Shadow Cascades`。保存 scene 后，这些字段会回写到 `.scene.yaml`。

## 当前还不是多光源 shadow 系统

| 能力 | 当前状态 | 说明 |
|---|---|---|
| directional CSM | 可用 | 当前 shadow 教程只覆盖这一条 |
| point / spot shadow | 未完成 | 没有 cubemap shadow、spot shadow atlas 或对应 shader path |
| 多光源直接照明 | 未完成 | Point/Spot 数据可进入 `SceneLightsUBO`，但主 PBR/Deferred shader 未遍历 |
| 多光源 shadow 索引 | 未完成 | 需要在 light buffer 中明确 shadow index、flags 和 shadow resource 绑定策略 |

## 我们已经学会了什么

我们知道 shadow tuning 当前是 directional light 的一组明确字段，不是独立插件系统。教程场景可以通过 YAML、command 和 Inspector 调整这些字段；Point/Spot shadow、统一 light buffer 的 shadow index 和 area/probe 类光照都不属于当前 shadow 教程范围。

## 下一步

继续阅读 [架构总览](../../concepts-design/architecture.md)，把 shadow-era 教程放回 core / infra / backend 的整体分工里。

# 打开 Shadow 教程场景：先看见投影

Shadow scene 像一张固定舞台布置图。我们不从空场景开始搭，而是先打开仓库里的教程场景，让相机、地面、投影物和主光源都处在已知位置。

## 场景文件放在哪里

教程场景是一个普通 scene asset：

```text
assets/scenes/shadow_tutorial.scene.yaml
```

它使用当前 canonical `root:` 格式。关键节点如下：

| 节点 | 角色 | 关键字段 |
|---|---|---|
| `game_camera` | 固定观察视角 | `camera.eye` / `camera.target` |
| `ground_receiver` | 地面接收阴影 | `mesh.uri: builtin://lxe_editor/primitives/plane` |
| `cube_caster` | 物体投射阴影 | `mesh.uri: builtin://lxe_editor/primitives/cube` |
| `dir_light` | 主 directional light | `light.shadowStrength` / `light.shadowDistance` / `light.shadowCascadeCount` |

## YAML 到 runtime 对象

```yaml
light:                              # -> LightNodeState
  kind: Directional                  # -> DirectionalLight
  direction: [-0.45, -1.0, -0.35]    # -> DirectionalLight::setDirection()
  intensity: 1.2                     # -> DirectionalLight::setIntensity()
  shadowStrength: 0.7                # -> DirectionalLight::setShadowStrength()
  shadowDistance: 80.0               # -> DirectionalLight::setShadowDistance()
  shadowCascadeCount: 4              # -> DirectionalLight::setShadowCascadeCount()
```

地面和 cube 都使用 `assets/materials/blinnphong_lit.material`。这个材质同时定义 `Forward` 和 `Shadow` pass，所以它们既能进入 shadow depth pass，也能进入 forward pass。

## 在 editor 中验证

我们可以从项目内复制或打开这个 scene，然后使用当前 editor 的保存路径验证 round trip。核心检查点不是截图，而是 runtime 能重新读回这几个对象：

| 检查 | 预期 |
|---|---|
| scene load | `/ground_receiver`、`/cube_caster`、`/dir_light` 都存在 |
| scene save | 保存后的 YAML 仍包含 `light.shadowStrength` 等 shadow 字段 |
| scene reload | 重新加载后 directional light 仍有 4 个 cascade |

## 我们已经学会了什么

我们已经把 shadow 教程的舞台固定下来：地面负责接收，cube 负责投射，directional light 负责生成 CSM 参数，camera 负责给 CSM 计算提供观察范围。

## 下一步

进入 [02 Shadow pass 怎样写资源](02-shadow-pass-flow.md)。

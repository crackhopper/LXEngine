# 打开 Helmet standard PBR 场景：先固定验证对象

当前 PBR + IBL 验证对象是 Damaged Helmet。我们先固定相机、skybox、object-level IBL bake marker、directional light 和 PBR material，这样后续排查 bake、binding、tone mapping 或 OfflineRT 对比时观察点不会变化。

## 场景文件

```text
assets/scenes/generated/helmet_standard_pbr.scene.yaml
```

关键节点如下：

| 节点 | 角色 | 关键字段 |
|---|---|---|
| `game_cam` | 固定观察视角 | transform + `camera.fovY` / `camera.aspect` |
| `neutral_infinite_skybox` | infinite environment lighting feature 与 bake 请求 | `skybox.mode: infinite` + feature URI + bake marker |
| `finite_neutral_room` | finite skybox / 环境房间 | 普通 mesh/material + `skybox.mode: finite` |
| `damaged_helmet` | PBR + IBL 验收对象 | glTF mesh、standard-pbr material、`bake.ibl.enabled` |
| `compare_key_light` | 直射光对比 | `light.kind: Directional` |

## YAML 到 runtime 对象

```yaml
- nodeName: neutral_infinite_skybox
  skybox:
    mode: infinite
    feature:
      uri: assets/effects/environment_lighting.render-feature.yaml
    bake:
      enabled: true

- nodeName: finite_neutral_room
  skybox:
    mode: finite
  mesh:
    uri: assets/scenes/generated/finite_room/test_neutral_room.obj
  material:
    uri: assets/scenes/generated/finite_room/test_neutral_room_unlit.material

- nodeName: damaged_helmet
  bake:
    ibl:
      enabled: true
  mesh:
    uri: assets/models/damaged_helmet/DamagedHelmet.gltf
  material:
    uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
```

加载时，`SceneRuntime` 把 infinite skybox 注册为 `environmentLighting` feature，并把 object bake marker 写到 helmet 对象资源上。finite skybox 仍然是普通几何；它可以被 Forward 或未来 RT path 当普通 renderable 处理。`RenderWorkCompiler` 在准备 Forward pass 时校验 `feature.environmentLighting`、`feature.surfaceLighting`、scene environment bake 和 material IBL bake 是否满足当前 shader binding 合同。

## 一个 Scene 的四种输出

这个 scene 的 output profiles 让我们不用复制 scene 就能对比不同渲染流程：

| Profile | 结果 |
|---|---|
| `forward_no_ibl` | Forward 直射光，不启用 IBL |
| `ibl_only` | Forward 只看 IBL |
| `forward_ibl` | Forward 直射光 + IBL |
| `raytrace` | OfflineRT primary ray + direct lighting，miss 时采样 infinite skybox |

命令行验证：

```bash
for profile in forward_no_ibl ibl_only forward_ibl raytrace; do
  ./build/src/tools/lxe_offline_render/lxe_offline_render \
    --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
    --profile "$profile" \
    --out "artifacts/offline/$profile"
done
```

## 在 editor 中打开

从 build 目录启动 editor：

```bash
ninja lxe_editor
./build/src/editor/lxe_editor
```

在 Console 中直接打开当前 scene asset：

```text
scene open assets/scenes/generated/helmet_standard_pbr.scene.yaml
preview on
```

如果只想验证资产和 parser，不启动 editor，我们先跑测试路径：

```bash
./build/src/test/test_render_resource_parsers
./build/src/test/test_shader_compiler
```

这两个测试会确认 scene / graph / feature / material / shader ABI 保持当前合同。

## 当前能看到什么

场景应该能稳定打开并进入 PBR + IBL binding 链路。Helmet 会使用 standard-pbr material 和 baked IBL resources；directional light 和 IBL 可以通过 output profile 分开看。判断当前切片是否正确时，优先看 `render debug live-stats`、render resource parser 测试和 HDR target dump，再结合截图确认 helmet 表面不是纯黑、纯白或 flat color。

## 我们已经学会了什么

我们已经把 Helmet standard PBR 场景拆成了五个验证锚点：相机固定观察点，infinite skybox 提供场景级 IBL 请求，finite skybox 作为普通几何存在，helmet 节点提供 glTF mesh 和 standard-pbr material，directional light 提供直射光对比。这样排查时就不会只看截图，而是能沿着 scene document、runtime node、resource table 和 render input 逐层确认。

这个场景的价值是稳定复现 PBR + IBL binding 链路，并为 offline Forward/RT 对比提供同一个输入。它不是 reflection probe 教程，也不把所有 HDR environment lighting 未来能力都当作当前事实。

## 下一步

进入 [02 资源与 Shader 合同](02-resource-and-shader-contract.md)。

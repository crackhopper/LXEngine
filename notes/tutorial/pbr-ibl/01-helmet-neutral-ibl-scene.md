# 打开 Helmet neutral IBL 场景：先固定验证对象

当前 PBR + IBL 验证对象是 Damaged Helmet。我们先固定相机、environment node、object-level IBL bake marker 和 PBR material，这样后续排查 bake、binding 或 tone mapping 时观察点不会变化。

## 场景文件

```text
assets/scenes/generated/helmet_neutral_ibl_full.scene.yaml
```

关键节点如下：

| 节点 | 角色 | 关键字段 |
|---|---|---|
| `game_camera` | 固定观察视角 | transform + `camera.fovY` / `camera.aspect` |
| `neutral_environment` | environment lighting feature 与 environment bake 请求 | `environment.feature.uri` + `environment.bake.enabled` |
| `damaged_helmet` | PBR + IBL 验收对象 | glTF mesh、standard-pbr material、`bake.ibl.enabled` |
| `compare_key_light` | 直射光对比 | `light.kind: Directional` |

## YAML 到 runtime 对象

```yaml
- nodeName: neutral_environment
  environment:
    feature:
      uri: assets/effects/environment_lighting.render-feature.yaml
    bake:
      enabled: true

- nodeName: damaged_helmet
  bake:
    ibl:
      enabled: true
  mesh:
    uri: assets/models/damaged_helmet/DamagedHelmet.gltf
  material:
    uri: assets/scenes/generated/materials/damaged_helmet_standard_pbr.material
```

加载时，`SceneRuntime` 把 environment feature 注册到 `SceneResourceTable`，把 object bake marker 写到对象资源上。`RenderWorkCompiler` 在准备 Forward pass 时校验 `feature.environmentLighting`、`feature.surfaceLighting`、`scene.environmentBake` 和 `scene.materialIblBake` 是否满足当前 shader binding 合同。

## 在 editor 中打开

从 build 目录启动 editor：

```bash
ninja lxe_editor
./build/src/editor/lxe_editor
```

在 Console 中直接打开当前 scene asset：

```text
scene open assets/scenes/generated/helmet_neutral_ibl_full.scene.yaml
preview on
```

如果只想验证资产和 parser，不启动 editor，我们先跑测试路径：

```bash
./build/src/test/test_render_resource_parsers
./build/src/test/test_shader_compiler
```

这两个测试会确认 scene / graph / feature / material / shader ABI 保持当前合同。

## 当前能看到什么

场景应该能稳定打开并进入 PBR + IBL binding 链路。Helmet 会使用 standard-pbr material 和 baked IBL resources；有直射光版本用于和 pure environment 版本对比。判断当前切片是否正确时，优先看 `render debug live-stats`、render resource parser 测试和 HDR target dump，再结合截图确认 helmet 表面不是纯黑、纯白或 flat color。

## 我们已经学会了什么

我们已经把 Helmet neutral IBL 场景拆成了四个验证锚点：相机固定观察点，environment node 提供场景级 IBL 请求，helmet 节点提供 glTF mesh 和 standard-pbr material，directional light 提供直射光对比。这样排查时就不会只看截图，而是能沿着 scene document、runtime node、resource table 和 render input 逐层确认。

这个场景的价值是稳定复现 PBR + IBL binding 链路。它不是 reflection probe 教程，也不把所有 HDR environment lighting 未来能力都当作当前事实。

## 下一步

进入 [02 资源与 Shader 合同](02-resource-and-shader-contract.md)。

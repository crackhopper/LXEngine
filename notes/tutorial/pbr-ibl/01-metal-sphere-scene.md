# 搭建金属球场景：先固定摄影棚

金属球场景是我们的摄影棚布置图。我们先固定相机、地面、主光和 HDR 环境入口，再让球使用 PBR gold 材质。这样后续接入真实 bake 或 skybox 时，观察点不会变化。

## 场景文件

```text
assets/scenes/ibl_metal_sphere.scene.yaml
```

关键节点如下：

| 节点 | 角色 | 关键字段 |
|---|---|---|
| `game_camera` | 固定观察视角 | node `transform` + `camera.fovY` / `camera.aspect` |
| `ground_reference` | 灰色参考地面 | `mesh.uri: builtin://lxe_editor/primitives/plane` |
| `metal_sphere` | PBR + IBL 验收对象 | `mesh.uri: builtin://lxe_editor/primitives/sphere` |
| `dir_light` | 保留直射光对比 | `light.kind: Directional` |

## YAML 到 runtime 对象

```yaml
scene:
  name: IBL Metal Sphere
  gameplayCameraPath: /game_cam
  environment:                              # -> EnvironmentState
    enabled: true                           # -> SceneRuntime 设置 scene-level IBL resources
    hdrUri: assets/env/studio_small_03_2k.hdr
    skyboxEnabled: true                     # -> Forward skybox 背景采样 baked SkyboxMap
    intensity: 1.0                          # -> EnvironmentData.intensity
    roughnessMipCount: 5.0                  # -> EnvironmentData.roughnessMipCount
```

金属球节点只声明 mesh 与 material：

```yaml
- nodeName: ibl_metal_sphere
  name: metal_sphere
  mesh:
    uri: builtin://lxe_editor/primitives/sphere
  material:
    uri: assets/materials/pbr_gold.material
```

运行时加载时，`SceneRuntime::loadFromDocumentPath(...)` 读取 `scene.environment`，加载 `hdrUri` 指向的 HDR texture，并保留两类资源：一类是 CPU preview/fallback cubemap，另一类是绑定名为 `EquirectangularMap` 的 HDR 输入。VulkanRenderer 初始化 scene 时会把这份 equirectangular 输入交给 GPU bake pipeline，生成 baked `SkyboxMap`、`IrradianceMap`、`PrefilteredEnvMap` 和 `BrdfLut`，后续 PBR draw input 会优先消费 baked resources。

## 在 editor 中打开

从 build 目录启动 editor：

```bash
ninja lxe_editor
./src/demos/lxe_editor/lxe_editor
```

新建可发现该 scene 的 project：

```text
project init pbr_ibl IBL Project
```

该模板会把 `ibl_metal_sphere` 注册进 project scene catalog。之后我们使用 Console：

```text
scene open ibl_metal_sphere
```

如果只想验证全局 scene asset，不启动 editor，我们先跑测试路径：

```bash
./build/src/test/test_scene_document
./build/src/test/test_scene_runtime
```

这两个测试会确认 scene asset 存在、environment 配置能 round-trip、`metal_sphere` 使用 `pbr_gold.material`，并且 PBR draw input 能收到 scene-level IBL resources。

## 当前能看到什么

场景已经能稳定打开并进入 PBR + IBL binding 链路；金属球会采样 renderer baked prefiltered cubemap，背景会通过 Forward HDR 的 skybox draw 采样同一份 baked `SkyboxMap`。我们判断当前切片是否正确，优先看 runtime/FrameGraph 事实，并结合截图确认背景方向性和金属反射是否一致。

## 下一步

进入 [02 资源与 Shader 合同](02-resource-and-shader-contract.md)。

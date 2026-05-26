# 搭建金属球场景：先固定摄影棚

金属球场景是我们的摄影棚布置图。我们先固定相机、地面、主光和 HDR 环境入口，再让球使用 PBR gold 材质。这样后续接入真实 bake 或 skybox 时，观察点不会变化。

## 场景文件

```text
assets/scenes/ibl_metal_sphere.scene.yaml
```

关键节点如下：

| 节点 | 角色 | 关键字段 |
|---|---|---|
| `game_camera` | 固定观察视角 | `camera.eye` / `camera.target` |
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
    skyboxEnabled: true                     # -> 当前记录配置；真实 skybox 渲染仍在接入中
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

运行时加载时，`SceneRuntime::loadFromDocumentPath(...)` 读取 `scene.environment`，加载 `hdrUri` 指向的 HDR texture，并用 HDR 平均辐射值生成临时 1x1 cubemap IBL resources。当前这不是最终 bake 质量，只是让金属球确实从 HDR 输入得到非黑环境光；真实 cubemap face/mip bake 会由 `REQ-048-a` 接上。

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

这两个测试会确认 scene asset 存在、environment 配置能 round-trip、`metal_sphere` 使用 `pbr_gold.material`，并且 PBR draw item 能收到 scene-level IBL resources。

## 当前能看到什么

在真实 GPU bake/skybox 接入前，场景已经能稳定打开并进入 PBR + IBL binding 链路；金属球的环境反射强度仍受默认 IBL resources 限制。我们判断当前切片是否正确，优先看 runtime/FrameGraph 事实，而不是只看截图。

## 下一步

进入 [02 资源与 Shader 合同](02-resource-and-shader-contract.md)。

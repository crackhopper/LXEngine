# PBR + IBL：从 HDR 环境到金属球

PBR + IBL 教程像搭一间带镜面球的摄影棚：HDR 环境提供棚内光线，IBL 资源把这份光线拆成 diffuse/specular 查表，PBR 材质把金属球表面算成线性 HDR 颜色，最后 PostProcess 把 HDR 画面映射到屏幕。

## 这一组教程解决什么

当前代码已经具备 HDR post stack、HDR/cubemap 资源形状、PBR shader 的 scene-level IBL binding 合同，以及一个可加载的 metal sphere scene。GPU bake 与 skybox 背景仍在 `REQ-048-a` / `REQ-050-a` 的后续切片中推进，因此本教程会把“当前能验证的链路”和“下一步接入的链路”分开讲。

| 对象 | 作用 | 当前文件 |
|---|---|---|
| HDR environment | IBL 输入资产 | `assets/env/studio_small_03_2k.hdr` |
| Post stack | HDR scene color 到 swapchain 的显示管线 | `Pass_PostProcess` / bloom passes |
| PBR material | 金属球的材质参数和 Forward pass | `assets/materials/pbr_gold.material` |
| IBL bindings | scene-level 资源注入点 | `IrradianceMap` / `PrefilteredEnvMap` / `BrdfLut` / `EnvironmentUBO` |
| Metal sphere scene | 可打开的验证场景 | `assets/scenes/ibl_metal_sphere.scene.yaml` |

## 阅读顺序

1. [搭建金属球场景](01-metal-sphere-scene.md)：从 scene YAML 看相机、环境、地面和 PBR 金属球。
2. [资源与 Shader 合同](02-resource-and-shader-contract.md)：解释 `.material`、PBR shader binding、scene-level IBL 资源的边界。
3. [HDR 到屏幕的 Post 流程](03-hdr-post-process-flow.md)：解释 Forward HDR、tone mapping、gamma 和 bloom。
4. [验证与排错](04-verify-and-debug.md)：用测试、editor 命令和 render dump 验证链路。

## 当前能力边界

| 能力 | 当前状态 | 说明 |
|---|---|---|
| PBR shader 输出线性 HDR | 可用 | PBR fragment shader 不做 tone mapping |
| scene-level IBL resource 注入 | 可用 | PBR draw item 会收到默认或显式 IBL resources |
| HDR cubemap / mip 资源形状 | 可用 | backend 支持 2D/cube、mip/layer 形状 |
| GPU bake shader 合同 | 可用 | equirect、irradiance、prefilter、BRDF LUT shader 已编译和反射测试 |
| GPU bake 执行与真实 skybox | 接入中 | 见 [REQ-048-a](../../requirements/048-a-ibl-gpu-bake-pipeline.md) 和 [REQ-050-a](../../requirements/050-a-ibl-metal-sphere-test-scene.md) |
| local reflection probe | 未实现 | 后续会作为独立需求推进 |

## 下一步

进入 [01 搭建金属球场景](01-metal-sphere-scene.md)。

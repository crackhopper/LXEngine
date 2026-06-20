# PBR + IBL：从 neutral environment 到 Damaged Helmet

PBR + IBL 教程像搭一间固定灯光的产品摄影棚：environment feature 提供棚内光线，IBL bake 资源把这份光线拆成 diffuse/specular 查表，PBR 材质把 Damaged Helmet 表面算成线性 HDR 颜色，最后 Forward/Bloom 路径写到屏幕。

## 这一组教程解决什么

当前代码已经具备 cubemap/mip 资源形状、PBR shader 的 scene-level IBL binding 合同、Vulkan IBL bake、Forward HDR 目标、Bloom 输出，以及可加载的 Helmet 标准 PBR 场景。`SceneRuntime` 从 infinite skybox node 和 object bake marker 收集 bake 请求；`IblBakeJobService` 通过 `FrameGraphExecutor` 执行 bake，并让 PBR draw input 消费 baked scene-level IBL resources。

| 对象 | 作用 | 当前文件 |
|---|---|---|
| Environment feature | IBL 输入资产与环境参数 | `assets/effects/environment_lighting.render-feature.yaml` |
| Surface lighting feature | PBR 是否启用 IBL 和 bake ready flags | `assets/effects/surface_lighting.render-feature.yaml` |
| PBR material | Helmet 的 standard-pbr 参数和贴图 | `assets/scenes/generated/materials/damaged_helmet_standard_pbr.material` |
| IBL bindings | scene-level 资源注入点 | `IrradianceMap` / `PrefilteredEnvMap` / `BrdfLut` / `EnvironmentLightingUBO` / `SurfaceLightingUBO` |
| Helmet IBL scene | 可打开的验证场景 | `assets/scenes/generated/helmet_standard_pbr.scene.yaml` |

## 阅读顺序

1. [打开 Helmet standard PBR 场景](01-helmet-neutral-ibl-scene.md)：从 scene YAML 看相机、skybox node、object bake marker、PBR Helmet 和 output profiles。
2. [资源与 Shader 合同](02-resource-and-shader-contract.md)：解释 `.material`、PBR shader binding、scene-level IBL 资源的边界。
3. [HDR 到屏幕的 Post 流程](03-hdr-post-process-flow.md)：解释 Forward HDR、tone mapping、gamma 和 bloom。
4. [验证与排错](04-verify-and-debug.md)：用测试、editor 命令和 render dump 验证链路。

## 当前能力边界

| 能力 | 当前状态 | 说明 |
|---|---|---|
| PBR shader 输出线性 HDR | 可用 | PBR fragment shader 不做 tone mapping |
| scene-level IBL resource 注入 | 可用 | PBR draw input 会收到默认或显式 IBL resources |
| HDR cubemap / mip 资源形状 | 可用 | backend 支持 2D/cube、mip/layer 形状 |
| GPU bake shader 合同 | 可用 | equirect、irradiance、prefilter、BRDF LUT shader 已编译和反射测试 |
| CPU equirect cubemap fallback | 可用 | runtime 保留方向性 `SkyboxMap` / `PrefilteredEnvMap` preview 数据 |
| Environment direct lighting | 可用 | `environmentLighting` feature 可为 Forward 提供 environment map 和 direct-light fallback |
| GPU bake 执行 | 可用 | renderer 初始化时生成 baked skybox / irradiance / prefilter / BRDF LUT |
| local reflection probe | 未实现 | 后续会作为独立需求推进 |

## 下一步

进入 [01 打开 Helmet standard PBR 场景](01-helmet-neutral-ibl-scene.md)。

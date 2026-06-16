# REQ-073-g: Reflection Probe And Bake Render Path

> 2026-06-16 校准：本 REQ 属于 `REQ-073` 当前渲染闭环。它接在 `REQ-073-f` 的 environment / skybox 直接光照之后，实现 reflection probe、probe bake cache 和 graph-authored bake render path；package 后清理由 `REQ-074-g` 继续承接。

## 背景

当前 IBL bake 仍有私有后端路径：`IblBakeRenderer` 直接硬编码 `equirect_to_cubemap`、`ibl_irradiance_convolve`、`ibl_prefilter_env` 和 `ibl_brdf_lut` shader，并在 renderer 初始化阶段运行。这个路径能产出资源，但 graph、feature 参数、shader reflection、bake 输出 metadata 和 scene cache 不是同一个事实源。

本 REQ 把 reflection probe 与 bake path 改为 RenderPathGraph 正向合同：probe capture、environment filtering、BRDF LUT bake、bake cache 和 SceneResourceTable 注册都必须可诊断、可校验。

## 目标

1. 新增 `ReflectionProbeComponent` 与 scene parser / saver。
2. 新增 `ReflectionCapture`、`ReflectionFilter`、`BrdfLutBake` graph assets。
3. 用 RenderWorkCompiler typed inputs 表达 cubemap face / mip / probe bake iteration。
4. 把 bake 输出写入 scene-adjacent cache，并以 live payload 注册到 SceneResourceTable。
5. 删除或拒绝 `IblBakeRenderer::bakeStaticEnvironment()` 作为默认正向 bake path。

## 非目标

- 不把 probe lighting 合成到 Forward/Deferred shader；由 `REQ-073-h` 处理。
- 不实现 parallax-corrected box projection、DDGI、lightmap 或动态 probe update。
- 不解决所有 RenderFeature 参数架构；由 `REQ-073-i` 处理。
- 不实现 package 文件格式；现有 package 需求继续保持独立。

## 需求

### R1: Reflection Probe Scene Contract

scene YAML SHALL 支持严格的 `reflectionProbe` component。

最低字段：

```yaml
components:
  reflectionProbe:
    global: false
    capture:
      resolution: 256
      nearClip: 0.1
      farClip: 50.0
      includeSky: true
    influence:
      shape: sphere
      radius: 12.0
      blendDistance: 2.0
```

未知字段必须 fail-fast。Probe component 不是 ordinary CameraComponent，不能被普通 view camera collection 消费。

### R2: Bake RenderPathGraph Assets

本 REQ SHALL 新增或迁移以下 graph assets：

| RenderPath | 用途 |
|---|---|
| `ReflectionCapture` | 从 probe capture camera 渲染 scene radiance cubemap |
| `ReflectionFilter` | 从 radiance cubemap 生成 prefiltered map 和 diffuse SH |
| `BrdfLutBake` | 生成 BSDF split-sum LUT |

要求：

- pass list、shader URI、sources、targets、resources、face/mip iteration 都来自 graph。
- `renderPath: IBLBake`、root `ibl_*` shader short name 和直接 `.frag` 路径必须被拒绝。
- graph resource declarations 必须严格解析；未声明 source/target 不得被 backend 临时补齐。

### R3: Bake Parameters Are RenderFeature-Owned

probe bake 参数 SHALL 由 RenderFeature 或 graph-declared settings 承载，并进入 reflection validation。

最低参数：

- cubemap resolution。
- prefilter mip count。
- specular sample count。
- diffuse SH coefficient count。
- BRDF LUT size / BSDF model。

不得在 `IblBakeRenderer`、`VulkanRealtimeRenderer` 或 bake executor 中硬编码 sample count、mip count 或 shader name 作为正向事实源。

### R4: Scene Bake Cache

scene-adjacent cache SHALL 保存 probe bake 输出。

要求：

- cache root 使用 `.lxe-bake/<scene-stem>/`。
- manifest parser strict：schema、binding、format、mip、face、file path 缺失或未知字段都失败。
- loader 只能注册 live texture / SH / LUT payload；metadata-only record 不得满足 `scene.reflectionProbes`。
- missing cache 不触发隐式 bake；invalid cache 让 scene load fail-fast。

### R5: Hard Cut Private Bake Path

完成态 default bake path SHALL NOT 调用 `IblBakeRenderer::bakeStaticEnvironment()`。

旧路径处理：

- old `IblBakeRenderer` 可被替换为 graph executor 内部 implementation detail，但不能保留 public/default shortcut。
- positive tests 不得直接证明私有 renderer 成功。
- 保留的 legacy token 只能出现在 named negative audit 或历史说明中。

## 测试

- scene parser accepts/rejects `reflectionProbe` component。
- graph parser rejects `IBLBake` and root `ibl_*` shader URI。
- RenderWorkCompiler expands cubemap face/mip bake inputs deterministically。
- bake cache loader rejects missing face, wrong binding, unknown field and metadata-only payload。
- Vulkan bake smoke executes graph-authored tiny probe bake and registers live SceneResourceTable resources。
- rg audit: `IblBakeRenderer|bakeStaticEnvironment|renderPath: IBLBake|ibl_prefilter_env|ibl_brdf_lut` no production/default positive path hits.

## 修改范围

- scene component parser/saver
- RenderPathGraph resource schema
- RenderWorkCompiler bake input metadata
- bake graph assets and shaders under `render_paths/`
- scene bake cache loader/writer
- SceneResourceTable probe / BRDF LUT live resources
- Vulkan graph bake executor and tests

## 边界与约束

- RenderFeature / graph settings 是 bake 参数事实源。
- Shader、feature、graph resource dependencies 必须解析为 live typed payload。
- backend 不得手动创建 material instance 或 placeholder resource 来满足 bake pass。
- 不新增第二套 public bake graph。

## 依赖

- `REQ-073-f`: environment map / skybox direct lighting。

## 后续工作

- `REQ-073-h`: IBL lighting post effect。
- `REQ-073-i`: RenderFeature parameter architecture hard cut。

## 实施状态

未实施。

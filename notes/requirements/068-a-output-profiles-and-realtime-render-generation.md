# REQ-068-a: Output Profiles 与 Realtime Render 生成

> 2026-06-02 新增：把 scene 文件里的旧 `offlineRender.profiles` 拆成输出配置与离线算法配置两层，并在 editor 中增加基于 output profile 的实时离屏渲染导出命令，用于和 offline renderer 输出做对比。

## 背景

当前 `.scene.yaml` 的 `scene.offlineRender.profiles` 同时保存了输出配置和离线渲染算法配置：

| 字段 | 当前含义 | 问题 |
|---|---|---|
| `width` / `height` / `outputFormat` | 输出尺寸与格式 | 这些字段同样适用于 realtime render dump，不应只属于 offline render |
| `camera` / CLI `--camera` | 渲染视角 | profile 没有明确持有相机引用，CLI 和 scene 配置容易分叉 |
| `integrator` / `samples` / `maxDepth` | 离线算法参数 | 和输出尺寸、输出目录混在同一个 profile 里 |
| `backend` | 离线 backend 选择 | 当前只有一个 backend，不需要成为 scene profile 字段 |

我们需要把“输出从哪里看、输出到哪里、输出成什么格式”和“离线算法怎么跑”拆开。这样同一个 output profile 可以同时服务：

- `lxe_offline_render` 的离线渲染。
- editor 里的 realtime offscreen render 导出。
- 后续 realtime/offline 图像对比与回归测试。

## 目标

1. 用 `scene.outputProfiles` 替代旧 `scene.offlineRender.profiles`。
2. `scene.offlineRender` 只保存单个离线算法默认配置。
3. CLI 用 `--profile` 选择 output profile，用 `--max-bounce` 替代 `--max-depth`。
4. editor 新增 `realtime-render ls` 和 `realtime-render run <profile>`。
5. `realtime-render run` 按 output profile 创建实时离屏 render target，而不是 dump swapchain。
6. realtime 输出同时保留 linear、CPU tone-mapped sRGB、pipeline sRGB 三路结果。
7. 拆分现有大型 editor command 注册文件，为新增命令提供可检索的落点。
8. 提供 Codex 可直接调用的 CLI/API 验证入口，能在本地生成 realtime/offline 输出并对比 EXR 数据。

## 需求

### R1: Scene output profile schema

`.scene.yaml` SHALL 使用 `scene.outputProfiles` 描述输出配置。

建议 schema：

```yaml
scene:
  defaultOutputProfile: preview
  outputProfiles:
    preview:
      camera: /game_cam
      width: 512
      height: 512
      outputFormat: exr-png
      outDir: artifacts
      cameraOverrides:
        fovY: 42.0
        nearPlane: 0.1
        farPlane: 120.0

  offlineRender:
    integrator: primary-ray
    samples: 1
    maxBounce: 1
    seed: 1
    profile: preview
```

字段语义：

| Field | Meaning |
|---|---|
| `defaultOutputProfile` | CLI/editor 未显式指定 profile 时使用的 output profile |
| `outputProfiles.<name>.camera` | scene 中已有 camera node path |
| `width` / `height` | 输出 render target 尺寸 |
| `outputFormat` | 输出格式，首版至少支持 `png` 和 `exr-png` |
| `outDir` | 输出目录，默认 `artifacts` |
| `cameraOverrides` | 可选 camera component 参数覆盖，不覆盖 transform |

`cameraOverrides` SHALL 只覆盖 camera projection 参数，例如 `fovY`、`aspect`、`nearPlane`、`farPlane`、`orthographicHeight`、`cullingMask`。它 SHALL NOT 覆盖 camera node transform。

旧 `scene.offlineRender.profiles` SHALL 不再作为兼容路径保留。读取到旧 schema 时 SHALL fail-fast，并给出明确迁移诊断。

### R2: Offline render config schema

`scene.offlineRender` SHALL 保存单个离线算法默认配置，而不是多个 profile。

字段语义：

| Field | Meaning |
|---|---|
| `integrator` | 离线算法，例如 `primary-ray` / `path-tracing` |
| `samples` | 每像素采样数 |
| `maxBounce` | path tracing 最大反弹次数，替代旧 `maxDepth` |
| `seed` | 随机种子 |
| `profile` | 引用 `scene.outputProfiles` 中的 profile 名 |

`backend` SHALL 从 scene 配置中移除。当前 Vulkan compute 是唯一 offline backend，backend 选择不进入 output profile 或 offlineRender 默认配置。

### R3: CLI migration

`lxe_offline_render` SHALL 读取新 schema：

```bash
lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile reference \
  --samples 64 \
  --max-bounce 4
```

CLI override 规则：

| CLI option | Overrides |
|---|---|
| `--profile` | `offlineRender.profile`，即 output profile 名 |
| `--width` / `--height` | selected output profile 的尺寸 |
| `--samples` | `offlineRender.samples` |
| `--max-bounce` | `offlineRender.maxBounce` |
| `--seed` | `offlineRender.seed` |
| `--out` | 显式输出路径，优先于 profile `outDir` |

`--max-depth` SHALL 删除，不保留 deprecated alias。

### R4: Editor realtime-render commands

editor SHALL 新增命令：

```text
realtime-render ls
realtime-render run <profile>
```

`realtime-render ls` SHALL：

- 读取当前 active scene document 的 `outputProfiles`。
- 返回 profile 名、camera、width、height、outputFormat、outDir。
- 在 structured JSON 中返回完整列表，方便本地 CLI/API 和网页侧读取。

`realtime-render run <profile>` SHALL：

- 按 profile 查找 camera node。
- 临时应用 `cameraOverrides`，不修改 scene 文件，不污染当前 editor viewport camera。
- 按 profile 尺寸创建 offscreen realtime render target。
- 复用实时渲染管线渲染一帧。
- 输出到 profile `outDir`。
- 返回导出文件路径的 structured JSON。

命令 SHALL 不读取 swapchain，也不 dump 当前 viewport。

### R5: Realtime profile output contract

`realtime-render run` SHALL 面向 offline/realtime 对比导出三类结果：

| Output | Source | Purpose |
|---|---|---|
| `linear.exr` | realtime linear buffer | 与 offline EXR 做主要数值/视觉对比 |
| `cpu_srgb.png` | CPU 从 linear buffer tone-map / encode 得到 | 验证 CPU 输出路径与 offline 共用处理 |
| `pipeline_srgb.png` | realtime pipeline 最终 sRGB 输出 | 对比屏幕路径中的 shader tone mapping |

文件名 SHALL 包含 scene 名、profile 名和稳定的运行标识，避免覆盖已有输出。

### R6: Linear output path

realtime pipeline SHALL 提供 linear 输出通道。首版 MAY 使用额外 SSBO 或等价 readback buffer。

要求：

- linear 输出 SHALL 在 tone mapping 前写出。
- 如果该输出未启用，shader MAY 通过 specialization constant 或等价机制跳过写入。
- 新通道 SHALL 不改变普通 editor viewport 的视觉结果。

### R7: Tone mapping shared semantics

shader 侧和 CPU 侧 SHALL 使用同一套 tone mapping 语义。

实现要求：

- shader 侧提供可复用 include，例如 `assets/shaders/glsl/common/tone_mapping.glsl`。
- pipeline sRGB 输出使用该 shader include。
- CPU 侧提供对应 tone mapping / linear-to-sRGB helper，供 realtime profile output 和 offline image output 复用。
- 测试 SHALL 覆盖一组固定 linear color sample，验证 CPU 和 shader 侧语义不会明显漂移。

首版不要求 codegen。我们用同名函数、测试样例和文档约束保证语义一致。

### R8: Command registration split

现有 editor command 注册文件 SHALL 拆分，避免继续向大型单文件追加命令。

建议结构：

```text
src/core/editor/commands/
  builtin_commands.hpp
  register_builtin_commands.cpp
  command_helpers.hpp/.cpp
  scene_commands.cpp
  node_commands.cpp
  camera_commands.cpp
  selection_commands.cpp
  debug_probe_commands.cpp
  undo_redo_commands.cpp

src/demos/lxe_editor/commands/
  register_lxe_editor_commands.cpp
  project_commands.cpp
  scene_project_commands.cpp
  recording_commands.cpp
  display_commands.cpp
  render_debug_commands.cpp
  realtime_render_commands.cpp
```

拆分规则：

| Command area | Location |
|---|---|
| 通用 scene/node/camera/selection/debug probe/undo redo | `src/core/editor/commands/` |
| project/session/recording/display/render debug/realtime profile output | `src/demos/lxe_editor/commands/` |

`realtime_render_commands.cpp` SHALL 只注册 `realtime-render`，并通过明确 hook/interface 访问 render profile generation 能力，避免直接耦合大量 session internals。

拆分 SHALL 先做搬迁和等价测试，再添加新命令。

### R9: Codex-callable local realtime render API / CLI

realtime profile generation SHALL 暴露一个 Codex 可直接调用的本地入口。该入口 SHALL NOT 依赖 MCP，因为 MCP server 可能运行在用户机器而不是 Codex 执行环境中。

可接受入口：

- 独立 CLI，例如 `lxe_realtime_render`。
- 本地进程控制 wrapper：启动本机 `lxe_editor`，通过本机 HTTP/API/command endpoint 执行 `realtime-render run <profile>`，然后关闭该 `lxe_editor` 进程。

入口必须满足：

- 不需要人工点击 UI。
- 可指定 scene path 和 output profile。
- 可从命令行或本机 API 获得 structured JSON 结果。
- JSON 结果包含 `linear.exr`、`cpu_srgb.png`、`pipeline_srgb.png` 的绝对或工程相对路径。
- 失败时返回明确错误，而不是只写 editor console。
- 如果入口启动了 `lxe_editor`，无论成功或失败，结束时都必须关闭它启动的 editor 进程。
- 入口不得关闭用户已经手动打开、且不是该入口启动的 editor 进程。

建议 CLI 形式：

```bash
lxe_realtime_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile preview
```

若首版复用本机 editor 进程，则本地 API/HTTP command SHALL 能等价执行：

```text
realtime-render run preview
```

并且 Codex SHALL 能通过该返回结果定位输出文件。Codex 本地验证流程 SHALL 使用上述 CLI/API，不依赖 MCP。

### R10: Realtime/offline EXR comparison

需求实现后 SHALL 支持一个本地验证流程：

1. 用同一 scene 和 output profile 运行 realtime profile generation，生成 `linear.exr`。
2. 用 `lxe_offline_render` 运行同一 scene 和 output profile，生成 offline linear EXR。
3. 用测试工具或 CLI 对两个 EXR 做数值对比。

比较工具 SHALL 输出至少以下指标：

| Metric | Meaning |
|---|---|
| `meanAbsError` | RGB 平均绝对误差 |
| `maxAbsError` | RGB 最大绝对误差 |
| `rmse` | RGB 均方根误差 |
| `pixelCount` | 参与比较的像素数 |

首版比较 SHALL 支持设置容差，并在超出容差时 fail-fast。

为了让比较有意义，参与对比的 profile / scene SHALL 满足：

- output profile 使用同一 camera、同一分辨率、同一 scene 材质。
- offlineRender 使用 `samples: 1`、`maxBounce: 1`，或一个能与 realtime 公式直接对齐的配置。
- realtime 和 offline 使用一致的 direct lighting / BRDF 公式。
- shadow MAY 通过配置开关关闭；如果开启，realtime 和 offline shadow 采样策略必须明确记录，否则不作为数值对比验收目标。
- 用于对比的测试 scene SHOULD 使用简单、稳定的材质参数，避免 texture filtering、IBL mip、随机采样等因素掩盖基础公式差异。

如需引入 shadow 开关，建议放在 `offlineRender` 或 compare profile 的明确字段中，例如：

```yaml
scene:
  offlineRender:
    integrator: primary-ray
    samples: 1
    maxBounce: 1
    seed: 1
    profile: preview
    shadows: false
```

realtime path SHALL 提供等价的 profile generation override 或 render feature toggle，确保对比时两个路径使用同一光照项集合。

### R11: Shared shading formula for comparison

realtime/offline 对比所使用的基础 shader 公式 SHALL 共享语义。

要求：

- realtime shader 中 direct lighting / BRDF 的核心公式 SHOULD 抽成 shader include 或可复用 helper。
- offline compute shader SHALL 使用等价公式，或在文档和测试中列出允许差异。
- 对比测试 scene 的材质参数 SHALL 选择能稳定暴露公式差异的值，例如固定 baseColor、metallic、roughness、emissive。
- 如果 offline ray tracing 临时关闭 shadow，realtime pipeline 也 SHALL 在 profile generation 中关闭对应 shadow contribution。

## 测试

### T1: Scene schema parsing

- 新 schema 可加载。
- `outputProfiles` 可 round-trip。
- `offlineRender.profile` 必须引用存在的 output profile。
- 旧 `offlineRender.profiles` 给出明确错误。
- 缺失 `outDir` 时默认 `artifacts`。

### T2: CLI override

- `--profile` 覆盖 output profile。
- `--max-bounce` 覆盖 `offlineRender.maxBounce`。
- `--max-depth` 被拒绝。
- `--out` 优先于 profile `outDir`。

### T3: Editor command smoke

- `realtime-render ls` 返回当前 scene profile 列表。
- `realtime-render run preview` 生成 structured JSON。
- 命令运行后 active viewport camera 状态不被污染。

### T4: Realtime output files

- `linear.exr` 非空。
- `cpu_srgb.png` 非空。
- `pipeline_srgb.png` 非空。
- 输出路径位于 profile `outDir`。

### T5: Tone mapping consistency

- 固定 linear color samples 的 CPU tone mapping 输出符合预期。
- shader tone mapping include 被 pipeline sRGB 路径使用。

### T6: Command split regression

- 现有核心命令仍能 dispatch。
- 现有 lxe_editor project/scene/render debug/display/recording 命令仍能 dispatch。
- 新拆分文件不会引入循环依赖。

### T7: Codex-callable render generation

- CLI/API 能在无人工 UI 操作下生成 realtime profile outputs。
- 返回 structured JSON，并包含三路输出路径。
- Codex 能读取输出文件并用于后续比较。
- 如果测试启动了 `lxe_editor`，测试结束后对应进程已关闭。

### T8: Realtime/offline EXR comparison

- 同一 scene/profile 可分别生成 realtime `linear.exr` 和 offline EXR。
- 比较工具输出 `meanAbsError`、`maxAbsError`、`rmse`、`pixelCount`。
- 在简单对比 scene 上，关闭 shadow 且使用 `samples=1` / `maxBounce=1` 时，误差低于需求定义的容差。
- 如果开启 shadow 或更复杂材质，测试必须明确标记为 visual/regression comparison，而不是基础数值等价验收。

## 修改范围

- `src/core/offline/offline_render_profile.*`
- `src/infra/scene_io/scene_document.*`
- `src/tools/lxe_offline_render/`
- `src/tools/lxe_realtime_render/` 或本地 editor process/API wrapper
- `src/infra/offline/offline_image_writer.*`
- realtime/offline EXR comparison helper 或测试工具
- `src/core/` 或 `src/infra/` 的 CPU tone mapping helper
- `assets/shaders/glsl/common/`
- realtime/offline shared shading include / helper
- realtime renderer / render target readback 相关 backend 代码
- `src/core/editor/commands/`
- `src/demos/lxe_editor/commands/`
- `assets/scenes/*.scene.yaml`
- `src/test/`
- `notes/tutorial/offline-renderer/`

## 边界与约束

- 本 REQ 不实现完整 unified render job registry。
- 本 REQ 不引入多个 offline backend。
- 本 REQ 不保留旧 `offlineRender.profiles` 兼容路径。
- 本 REQ 不要求 camera transform override。
- 本 REQ 不要求 UI 面板编辑 output profile；命令和 YAML 先行。
- 本 REQ 不要求 ordinary viewport 每帧都写 linear SSBO；只要求 profile generation 可启用。
- 本 REQ 的 EXR 数值对比只要求在受控 scene/profile 下成立；复杂 IBL、shadow、texture filtering 和随机采样可作为后续更宽松的 visual/regression 对比。

## 依赖

- `REQ-053-a: Offline Scene YAML and Render Profile`
- `REQ-054-b: Vulkan Compute Offline Renderer MVP`
- `REQ-055-a: Offline Output EXR and PNG`
- `REQ-058-a: Editor Offline Render Integration`
- `REQ-067-a: SceneResourceTable 与 Bindless-Ready 资源模型`
- `REQ-067-b: Offline Renderer 迁移到共享资源模型`
- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/notes-writing-style/spec.md`

## 实施建议

建议拆成两个实现提交：

1. schema / CLI / scene assets migration：完成 `outputProfiles`、`offlineRender.maxBounce`、CLI override 和旧 schema fail-fast。
2. editor realtime profile output：先拆分 command 注册文件，再实现 `realtime-render ls/run`、Codex-callable CLI/API 和三路输出。
3. realtime/offline comparison：增加受控对比 scene、shadow toggle / feature override、EXR compare helper，并把它纳入本地验证流程。

## 当前实施状态

- 已完成 `outputProfiles` schema、scene migration、offline CLI `--profile` / `--max-bounce`、旧 `--max-depth` 拒绝和旧 `offlineRender.profiles` fail-fast。
- 已完成 editor command 拆分，以及 `realtime-render ls` / `realtime-render run <profile>` 命令。
- 已完成 realtime profile offscreen generation 的首版输出：`render-linear.exr`、`render-cpu_srgb.png` 和 `render.json`。
- 已完成 Codex-callable 本地 wrapper：`src/tools/lxe_realtime_render/lxe_realtime_render.py`。该入口启动本机 `lxe_editor --api-enable`，导入 scene，执行 `realtime-render run <profile>`，校验输出文件和 metadata，然后关闭它启动的 editor 进程。
- 已完成 EXR 读取和比较入口：`LX_infra::image::readRgba32fExr` 与 `src/tools/lxe_compare_exr/`。比较工具输出 `meanAbsError`、`maxAbsError`、`rmse`、`pixelCount`，并支持可选阈值失败返回码。
- 已完成受控 realtime/offline 对比 scene：`assets/scenes/realtime_offline_compare_flat.scene.yaml`。该 scene 使用 `offlineRender.compareMode: albedo` 和稳定的黑色 lit material，使 realtime/offline linear EXR 在零阈值下通过 `test_realtime_offline_compare_flat`。
- 尚未完成 realtime `pipeline_srgb.png` readback；当前 metadata 中 `pipelineSrgbStatus` 标记为 unavailable。
- 尚未完成复杂 BRDF/IBL/shadow 的数值等价；IBL scene 仍只适合作为 visual/regression comparison，不作为基础数值等价验收目标。

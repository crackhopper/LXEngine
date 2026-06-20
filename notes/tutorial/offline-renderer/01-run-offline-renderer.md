# 运行离线渲染器：从 Output Profile 到 EXR/PNG

离线渲染器的第一步不是追求画面最终质量，而是确认实验管线稳定：同一份 scene 能被 editor 保存，也能被 CLI 加载进 `SceneResourceTable`，再按 output profile 选择 `RenderPathGraph`，通过 `FrameGraphExecutor` 执行 raster 或 compute pass，最后输出一张线性 HDR 图，并同时保存 EXR、PNG preview、metadata 和 raw readback。

## 构建目标

从仓库根目录构建 CLI 和相关测试：

```bash
cmake -S . -B build -G Ninja
cmake --build build --target CompileShaders lxe_offline_render test_render_work_compiler test_render_resource_parsers test_vulkan_ibl_bake test_shader_compiler -j2
ctest --test-dir build --output-on-failure -R 'test_render_work_compiler|test_render_resource_parsers|test_vulkan_ibl_bake|test_shader_compiler'
```

| 目标 | 验证内容 |
|---|---|
| `CompileShaders` | Forward、IBL bake 和 OfflineRT shader 都能反射并编译 |
| `test_render_work_compiler` | `RenderPathGraph` / `RenderWorkCompiler` 能生成当前 raster 与 compute work |
| `test_render_resource_parsers` | render path graph、feature、material 和 scene schema 能解析 |
| `test_vulkan_ibl_bake` | IBL bake 的 FrameGraphExecutor 路径仍可用 |

## Output Profile 是输出参数单

`assets/scenes/generated/helmet_standard_pbr.scene.yaml` 里有 `scene.outputProfiles`。这些配置不会影响 editor 的实时视口：`OutputProfile` 负责相机、分辨率、输出格式、`outDir` 和 `renderPathGraph`。CLI 的 `--profile` 选择一个 output profile；相机和 render path graph 都来自被选中的 output profile。

```yaml
scene:
  gameplayCameraPath: /game_cam
  defaultOutputProfile: forward_no_ibl   # -> CLI 不传 --profile 时使用
  outputProfiles:
    forward_no_ibl:               # -> Forward direct lighting 对比
      camera: /game_cam
      width: 192
      height: 192
      outputFormat: exr-png
      renderPathGraph: assets/render_paths/forward_offline_direct.render-path.yaml
      outDir: artifacts/reference/damaged_helmet_forward_no_ibl
    ibl_only:
      camera: /game_cam
      width: 192
      height: 192
      outputFormat: exr-png
      renderPathGraph: assets/render_paths/forward_offline_ibl_only.render-path.yaml
      outDir: artifacts/reference/damaged_helmet_ibl_only
    forward_ibl:
      camera: /game_cam
      width: 192
      height: 192
      outputFormat: exr-png
      renderPathGraph: assets/render_paths/forward_offline_direct_ibl.render-path.yaml
      outDir: artifacts/reference/damaged_helmet_forward_ibl
    raytrace:
      camera: /game_cam
      width: 192
      height: 192
      outputFormat: exr-png
      renderPathGraph: assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml
      outDir: artifacts/reference/damaged_helmet_raytrace
```

CLI 参数可以覆盖被选中 output profile 的宽高和输出路径。`--out` 显式覆盖 profile 的 `outDir`；不传 `--out` 时，输出写到所选 `OutputProfile.outDir` 下的 `render.*`。

## 跑一次最小 Smoke

```bash
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile raytrace \
  --width 64 \
  --height 64 \
  --out artifacts/offline/smoke
```

成功时 CLI 会打印所选 Vulkan device、最终尺寸、samples 和中心像素值，并生成：

```text
artifacts/offline/smoke.exr
artifacts/offline/smoke.png
artifacts/offline/smoke.json
artifacts/offline/smoke.rgba32f
```

`.exr` 是主输出，保存 scene-linear HDR beauty RGBA；`.png` 是同一份 readback 经 ACES tone mapping 和 gamma 2.2 处理后的预览图；`.json` 记录 scene、profile、samples、maxBounce、seed、合成后的 `buildInfo` 和输出文件；`.rgba32f` 是调试格式，每个像素四个 32-bit float，顺序为 RGBA。

## 数据流

| 阶段 | 代码入口 | 输出 |
|---|---|---|
| 读取 scene | `LX_infra::scene_io::SceneDocument` | editor scene 文档 |
| 选择 profile | `OutputProfile` | 相机、宽高、输出目录、render path graph |
| 加载离线 scene 数据 | `LX_infra::offline::OfflineSceneLoader` | `SceneResourceTable` |
| 资产解析 | `OfflineAssetResolver` | `builtin://` / `cache://` / project path 的本地路径 |
| Render path graph | `OutputProfile.renderPathGraph` | Forward raster graph 或 OfflineRT compute graph |
| Render input 构建 | `RenderWorkCompiler::buildInputs(...)` / `prepare(...)` | draw/dispatch work + accepted `RenderInputDesc` |
| 执行与 readback | `FrameGraphExecutor` | `FrameGraphExecutionPayload` |
| 文件输出 | `OfflineImageWriter` | `.exr` / `.png` / `.json` / `.rgba32f` |

## 和 Realtime Renderer 的边界

| Realtime | Offline |
|---|---|
| 以 swapchain / material pass / raster draw 为中心 | 以 output profile / render path graph / readback payload 为中心 |
| 需要 editor 视口和交互状态 | headless，适合自动化和高采样实验 |
| 消费实时材质 pipeline 合同 | 复用同一套 feature/material/pass 合同；OfflineRT 走 compute ray tracing graph |
| 以每帧稳定交互为目标 | 以可复现实验和高质量参考图为目标 |

我们复用 scene YAML、资产路径、基础数学类型、`RenderPathGraph` / `RenderInputDesc` 工单形态，以及 Vulkan pipeline / descriptor / command buffer 基础设施；不复用实时 renderer 的 swapchain 和 viewport 状态。这个边界能让离线路径既能跑 Forward 对比，也能跑 OfflineRT compute graph，方便后续接 path tracing、hardware RT、denoising 和 reference AOV。

## 常见问题

| 现象 | 检查点 |
|---|---|
| 找不到 shader SPIR-V | 先跑 `cmake --build build --target CompileShaders`；CLI 会从 `build/assets/shaders/glsl/` 查找 graph 引用的 shader |
| 没有 Vulkan 物理设备 | 在 Linux headless 环境确认 Vulkan loader / llvmpipe / 驱动可用 |
| 画面全黑或中心像素为 0 | 检查 scene 是否有 camera、mesh、directional light；再跑 `test_gltf_scene_asset_loader` 和 `test_render_resource_parsers` |
| EXR 打不开 | 先看同 basename 的 `.png`；再确认我们使用支持 OpenEXR 的图像查看器 |
| PNG 过亮或过暗 | 当前 preview 使用 exposure 1.0、ACES、gamma 2.2；EXR 不做 tone mapping |

## 我们已经学会了什么

我们已经把一次离线渲染拆成了可验证的链路：scene 先提供 output profile 和 render path graph，CLI 再把它们交给 headless `FrameGraphExecutor`，最后写出 EXR、PNG、metadata 和 raw float readback。这里的重点不是画质，而是确认 `SceneResourceTable`、RenderPathGraph、`RenderWorkCompiler`、executor readback 和 writer 之间的合同能跑通。

这也解释了为什么离线 renderer 不复用实时 renderer 的 swapchain 和 viewport 状态。它复用的是 scene、资源表、RenderPathGraph、pipeline 和 descriptor 基础设施；输出、readback 和实验参数属于 output profile。

## 下一步

- [EXR 与 PNG 输出](02-output-and-exr-viewers.md)
- [实现结构](03-implementation-flow.md)
- [PBR + IBL Helmet 场景](../pbr-ibl/01-helmet-neutral-ibl-scene.md)

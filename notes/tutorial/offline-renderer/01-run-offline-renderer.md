# 运行离线渲染器：从 Scene Profile 到 EXR/PNG

离线渲染器的第一步不是追求画面最终质量，而是确认实验管线稳定：同一份 scene 能被 editor 保存，也能被 CLI 加载进 `SceneResourceTable`，再通过 Vulkan compute 输出一张线性 HDR 图，并同时保存 EXR、PNG preview、metadata 和 raw readback。

## 构建目标

从仓库根目录构建 CLI 和相关测试：

```bash
cmake -S . -B build -G Ninja
cmake --build build --target CompileShaders lxe_offline_render test_offline_render_cli test_offline_image_writer test_offline_scene_loader test_offline_gpu_scene test_vulkan_offline_renderer -j2
ctest --test-dir build --output-on-failure -R 'test_offline_image_writer|test_offline_scene_loader|test_offline_gpu_scene|test_vulkan_offline_renderer|test_offline_render_cli'
```

| 目标 | 验证内容 |
|---|---|
| `CompileShaders` | `offline_primary_ray.comp` 被编译成 `build/assets/shaders/glsl/offline_primary_ray.comp.spv` |
| `test_offline_scene_loader` | scene YAML 能编译成 `SceneResourceTable` |
| `test_offline_gpu_scene` | IR 能打包成 GPU buffer，并构建 BVH |
| `test_vulkan_offline_renderer` | headless Vulkan renderer 能初始化 |
| `test_offline_image_writer` | readback 能写成 EXR、PNG、JSON 和 raw dump |
| `test_offline_render_cli` | CLI 参数和 profile override 行为稳定 |

## Output Profile 是输出参数单

`assets/scenes/ibl_metal_sphere.scene.yaml` 里有 `scene.outputProfiles` 和单个 `scene.offlineRender`。这些配置不会影响 editor 的实时视口：`OutputProfile` 负责相机、分辨率、输出格式和 `outDir`，`OfflineRenderSettings` 负责 integrator、samples、maxBounce、seed 和 shadow 开关。CLI 的 `--profile` 选择一个 output profile；相机来自被选中的 output profile。

```yaml
scene:
  gameplayCameraPath: /game_cam
  defaultOutputProfile: preview   # -> CLI 不传 --profile 时使用
  outputProfiles:
    preview:                      # -> LX_core::offline::OutputProfile
      camera: /game_cam
      width: 512
      height: 512
      outputFormat: exr-png       # -> CLI 写 .exr / .png / .json / .rgba32f
      outDir: artifacts/offline/preview
    mvp:
      camera: /game_cam
      width: 1024
      height: 576
      outputFormat: exr-png
      outDir: artifacts/offline/mvp
  offlineRender:                  # -> LX_core::offline::OfflineRenderSettings
    integrator: software-compute       # -> assets/shaders/glsl/offline_primary_ray.comp
    samples: 1
    maxBounce: 1
    seed: 1
    profile: preview
    shadows: true
```

CLI 参数可以覆盖被选中 output profile 的宽高，以及 `OfflineRenderSettings` 里的 samples、maxBounce、seed 和输出路径。`--out` 显式覆盖 profile 的 `outDir`；不传 `--out` 时，输出写到所选 `OutputProfile.outDir` 下的 `render.*`。

## 跑一次最小 Smoke

```bash
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile mvp \
  --samples 1 \
  --width 64 \
  --height 64 \
  --max-bounce 1 \
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
| 选择 profile | `OutputProfile` + `OfflineRenderSettings` | 相机、宽高、输出目录、samples、integrator 等参数 |
| 加载离线 scene 数据 | `LX_infra::offline::OfflineSceneLoader` | `SceneResourceTable` |
| 资产解析 | `OfflineAssetResolver` | `builtin://` / `cache://` / project path 的本地路径 |
| GPU records 导出 | `SceneResourceTable::buildUploadView()` | vertex、index、mesh、primitive、object、material、camera params buffer |
| BVH 构建 | `SceneSoftwareBvh` | `SceneSoftwareBvhNode` + 重排后的 primitive buffer |
| Compute 执行 | `software-compute` integrator | `OfflineReadbackImage` |
| 文件输出 | `OfflineImageWriter` | `.exr` / `.png` / `.json` / `.rgba32f` |

## 和 Realtime Renderer 的边界

| Realtime | Offline |
|---|---|
| 以 swapchain / FrameGraph / material pass 为中心 | 以 scene IR / integrator / readback 为中心 |
| 需要 editor 视口和交互状态 | headless，适合自动化和高采样实验 |
| 消费实时材质 pipeline 合同 | 消费离线打包后的 material 参数 |
| 以每帧稳定交互为目标 | 以可复现实验和高质量参考图为目标 |

我们复用 scene YAML、资产路径、基础数学类型、Vulkan device/buffer/command 基础设施；不复用实时 renderer 的 draw item、FrameGraph 和 swapchain 路径。这个边界能避免离线路径被实时约束绑死，也方便后续在 shader 里试 paper、path tracing、denoising 和 reference AOV。

## 常见问题

| 现象 | 检查点 |
|---|---|
| 找不到 compute shader SPIR-V | 先跑 `cmake --build build --target CompileShaders`；CLI 会从 `build/assets/shaders/glsl/` 查找离线 compute shader |
| 没有 Vulkan 物理设备 | 在 Linux headless 环境确认 Vulkan loader / llvmpipe / 驱动可用 |
| 画面全黑或中心像素为 0 | 检查 scene 是否有 camera、mesh、directional light；再跑 `test_offline_scene_loader` |
| EXR 打不开 | 先看同 basename 的 `.png`；再确认我们使用支持 OpenEXR 的图像查看器 |
| PNG 过亮或过暗 | 当前 preview 使用 exposure 1.0、ACES、gamma 2.2；EXR 不做 tone mapping |

## 继续阅读

- [EXR 与 PNG 输出](02-output-and-exr-viewers.md)
- [实现结构](03-implementation-flow.md)
- [PBR + IBL 金属球场景](../pbr-ibl/01-metal-sphere-scene.md)

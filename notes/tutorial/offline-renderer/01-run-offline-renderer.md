# 运行离线渲染器：从 Scene Profile 到 `.rgba32f`

离线渲染器的第一步不是追求画面最终质量，而是确认实验管线稳定：同一份 scene 能被 editor 保存，也能被 CLI 编译成离线 IR，再通过 Vulkan compute 输出一张线性 HDR 调试图。

## 构建目标

从仓库根目录构建 CLI 和相关测试：

```bash
cmake -S . -B build -G Ninja
cmake --build build --target CompileShaders lxe_offline_render test_offline_scene_compiler test_offline_gpu_scene test_vulkan_offline_renderer -j2
ctest --test-dir build --output-on-failure -R 'test_offline_scene_compiler|test_offline_gpu_scene|test_vulkan_offline_renderer|test_offline_render_cli'
```

| 目标 | 验证内容 |
|---|---|
| `CompileShaders` | `offline_primary_ray.comp` 被编译成 `build/assets/shaders/glsl/offline_primary_ray.comp.spv` |
| `test_offline_scene_compiler` | scene YAML 能编译成 `OfflineSceneIR` |
| `test_offline_gpu_scene` | IR 能打包成 GPU buffer，并构建 BVH |
| `test_vulkan_offline_renderer` | headless Vulkan renderer 能初始化 |
| `test_offline_render_cli` | CLI 参数和 profile override 行为稳定 |

## Scene Profile 是实验参数单

`assets/scenes/ibl_metal_sphere.scene.yaml` 里有 `scene.offlineRender`。这段配置不会影响 editor 的实时视口，它只告诉离线 CLI 选择什么 backend、integrator、分辨率、采样数和输出格式。

```yaml
scene:
  gameplayCameraPath: /game_cam
  offlineRender:                  # -> LX_core::offline::OfflineRenderSettings
    defaultProfile: preview       # -> CLI 不传 --profile 时使用
    profiles:
      preview:
        backend: vulkan-compute   # -> 当前唯一可执行 backend
        integrator: primary-ray   # -> assets/shaders/glsl/offline_primary_ray.comp
        width: 512
        height: 512
        samples: 1
        maxDepth: 1
        seed: 1
        outputFormat: exr-png     # -> 当前 CLI 仍写 .rgba32f 调试输出
      reference:
        backend: vulkan-compute
        integrator: path-tracing  # -> 预留给后续 path tracing shader
        width: 1920
        height: 1080
        samples: 64
        maxDepth: 4
        seed: 1
        outputFormat: exr-png
```

CLI 参数可以覆盖 profile 中的宽高、samples 和输出路径。这样我们可以保留高质量 `reference` profile，同时在开发阶段用小图快速 smoke。

## 跑一次最小 Smoke

```bash
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/ibl_metal_sphere.scene.yaml \
  --profile mvp \
  --samples 1 \
  --width 64 \
  --height 64 \
  --out artifacts/offline/smoke
```

成功时 CLI 会打印所选 Vulkan device、最终尺寸、samples 和中心像素值，并生成：

```text
artifacts/offline/smoke.rgba32f
```

`.rgba32f` 是调试格式：每个像素四个 32-bit float，顺序为 RGBA，数据是线性 HDR 值。它适合自动测试和后续 EXR 写入模块，不适合作为长期人工看图格式。REQ-055-a 会把 readback 接到 EXR/PNG 输出；在那之前，我们主要用 CLI 成功、文件大小、中心像素有限值和后续测试来确认链路。

## 数据流

| 阶段 | 代码入口 | 输出 |
|---|---|---|
| 读取 scene | `LX_infra::scene_io::SceneDocument` | editor scene 文档 |
| 选择 profile | `OfflineRenderProfile` | 宽高、samples、integrator 等参数 |
| 编译离线 IR | `LX_infra::offline::OfflineSceneCompiler` | `OfflineSceneIR` |
| 资产解析 | `OfflineAssetResolver` | `builtin://` / `cache://` / project path 的本地路径 |
| GPU 打包 | `GpuSceneBuilder` | triangle、material、camera params buffer |
| BVH 构建 | `ComputeBvhBuilder` | `GpuBvhNode` + 重排后的 triangle buffer |
| Compute 执行 | `backend::offline::VulkanOfflineRenderer` | `OfflineReadbackImage` |
| 文件输出 | `lxe_offline_render` CLI | `.rgba32f` |

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
| 画面全黑或中心像素为 0 | 检查 scene 是否有 camera、mesh、directional light；再跑 `test_offline_scene_compiler` |
| 输出文件打不开 | 当前是 raw `.rgba32f`，不是 EXR/PNG；等输出模块落地后再用普通图像工具看图 |

## 继续阅读

- [实现自己的 Path Tracing](02-implement-path-tracing.md)
- [PBR + IBL 金属球场景](../pbr-ibl/01-metal-sphere-scene.md)
- [场景文档到 Runtime](../../scene-system/document-runtime-flow.md)
- [REQ-055-a](../../requirements/055-a-offline-output-exr-and-png.md)

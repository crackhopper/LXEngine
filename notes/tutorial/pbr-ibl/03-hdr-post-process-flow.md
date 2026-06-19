# HDR 到屏幕：Forward 和 Bloom 是当前出片主线

PBR shader 负责算摄影棚里的光照，并按 `feature.toneMapping` 的参数执行当前 Forward shader 内的 tone mapping。随后 `Bloom` fullscreen pass 读取 `hdr.color`，按 `feature.bloom` 做当前的 bloom blit，最后写到 `swapchain.color`。

## FrameGraph 主线

默认渲染顺序是：

```text
Shadow -> Forward(hdr.color) -> Bloom(swapchain.color) -> DebugOverlay/ImGui
```

| Pass | 读 | 写 | 作用 |
|---|---|---|---|
| `Shadow` | scene geometry | `shadow.cascadeN` | directional light shadow depth |
| `Forward` | camera/light/shadow/IBL resources | `hdr.color` / `depth.main` | PBR 和其它材质输出线性 HDR |
| `Bloom` | `hdr.color` / `feature.bloom` | `swapchain.color` | 当前 fullscreen bloom blit 和最终出片 |
| `DebugOverlay` | overlay draw data | swapchain pass | editor/debug 叠加 |

如果关闭 bloom，当前 renderer 会走不带 bloom feature 的 Forward 输出路径；文档验证仍以 `hdr.color` dump 为准。

## Feature 参数

| 参数 | 作用 | 默认 |
|---|---|---|
| `ToneMappingUBO.exposure` | HDR 进入 tone mapping 前的曝光倍率 | `1.0` |
| `ToneMappingUBO.mode` | `0` 为 ACES，`1` 为 Reinhard | `0` |
| `BloomUBO.threshold` | bloom mask 阈值 | `1.0` |
| `BloomUBO.intensity` | bloom 叠加强度 | `0.0` |
| `BloomUBO.radius` | 当前 blit 中的 bloom 权重 | `1.0` |

这组参数来自 `assets/effects/tone_mapping.render-feature.yaml` 和 `assets/effects/bloom.render-feature.yaml`。当前 PBR Forward shader 包含 tone mapping feature；Bloom pass 使用 `features/bloom.glsl` 读取 `SceneColor` 和 `BloomUBO`。

## 验证当前流程

```bash
cmake --build build --target test_vulkan_frame_graph test_shader_compiler -j2
./build/src/test/test_shader_compiler
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

| 测试 | 覆盖点 |
|---|---|
| `test_shader_compiler` | Forward PBR、Bloom blit、tone mapping 和 bloom feature binding 保持 ABI |
| `test_vulkan_frame_graph` | Vulkan backend 编译出的 pass 顺序包含 HDR Forward、Bloom、DebugOverlay |
| `test_frame_graph` | core FrameGraph 能表达 sampled read / color write 的资源流 |

## 下一步

进入 [04 验证与排错](04-verify-and-debug.md)。

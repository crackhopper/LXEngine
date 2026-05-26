# HDR 到屏幕：PostProcess 是统一出片流程

PBR shader 只负责算摄影棚里的线性光照，不负责把照片冲印出来。冲印这一步由标准 post stack 完成：Forward 写 HDR，PostProcess 做曝光、tone mapping、gamma 和 bloom，再写 swapchain。

## FrameGraph 主线

默认渲染顺序是：

```text
Shadow -> Forward(scene.hdrColor) -> BloomThreshold -> BloomBlurH -> BloomBlurV -> PostProcess(swapchain) -> DebugOverlay/ImGui
```

| Pass | 读 | 写 | 作用 |
|---|---|---|---|
| `Shadow` | scene geometry | `shadow.cascadeN` | directional light shadow depth |
| `Forward` | camera/light/shadow/IBL resources | `scene.hdrColor` / `scene.depth` | PBR 和其它材质输出线性 HDR |
| `BloomThreshold` | `scene.hdrColor` | `bloom.threshold` | 提取高亮区域 |
| `BloomBlurH/V` | bloom ping-pong | `bloom.blur` | full-res blur v1 |
| `PostProcess` | `SceneColor` / `BloomColor` | `swapchain.color` | exposure、tone mapping、gamma、bloom composite |
| `DebugOverlay` | overlay draw data | swapchain pass | editor/debug 叠加 |

`VulkanRenderer::PostProcessSettings` 可以在 `initScene()` 前关闭 bloom。关闭后编译出的顺序回到：

```text
Shadow -> Forward(scene.hdrColor) -> PostProcess(swapchain) -> DebugOverlay/ImGui
```

## Post shader 参数

| 参数 | 作用 | 默认 |
|---|---|---|
| `exposure` | HDR 进入 tone mapping 前的曝光倍率 | `1.0` |
| `toneMappingMode` | `0` 为 ACES，`1` 为 Reinhard | `0` |
| `gamma` | 输出 gamma correction | `2.2` |
| `bloomIntensity` | bloom 合成强度 | `0.25` |

PBR fragment shader 不做最终显示映射。这样我们能让 BlinnPhong、PBR、skybox 和未来透明/后处理路径共享同一个输出规则。

## 验证当前流程

```bash
cmake --build build --target test_vulkan_frame_graph test_shader_compiler -j2
./build/src/test/test_shader_compiler
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

| 测试 | 覆盖点 |
|---|---|
| `test_shader_compiler` | PostProcess shader 反射出 `SceneColor`、tone mapping 参数和 bloom binding |
| `test_vulkan_frame_graph` | Vulkan backend 编译出的 pass 顺序包含 HDR Forward、bloom、PostProcess、DebugOverlay |
| `test_frame_graph` | core FrameGraph 能表达 sampled read / color write 的资源流 |

## 下一步

进入 [04 验证与排错](04-verify-and-debug.md)。

# 验证与排错：从事实链路定位问题

PBR + IBL 问题通常像摄影棚调试：先确认布景在，再确认灯光资源接上，最后看冲印参数。我们不要只凭一张截图判断，而是按 scene、material、FrameGraph、render dump 的顺序缩小范围。

## 最小验证命令

```bash
cmake --build build --target CompileShaders test_scene_document test_scene_runtime test_frame_graph test_shader_compiler -j2
./build/src/test/test_scene_document
./build/src/test/test_scene_runtime
./build/src/test/test_frame_graph
./build/src/test/test_shader_compiler
```

有窗口/Vulkan 环境时再跑：

```bash
xvfb-run -a ./build/src/test/test_vulkan_frame_graph
```

| 验证 | 说明 |
|---|---|
| scene document | `ibl_metal_sphere.scene.yaml` 存在，environment / PBR material 能 round-trip |
| scene runtime | `metal_sphere` 能加载，PBR draw item 收到 scene-level IBL resources |
| frame graph | Forward/Post/Bloom 的资源关系能编译 |
| shader compiler | PBR、post、bloom、IBL bake shader 合同稳定 |
| Vulkan frame graph | backend pass 顺序和 bloom toggle 可运行 |

## Editor 与 render dump

从 build 目录启动：

```bash
./src/demos/lxe_editor/lxe_editor
```

在 Console 中打开 project 内场景后，可以 dump 当前 FrameGraph attachment
或 debug render target pass。HDR attachment 会以调试用 tone mapping 写成 BMP。

```text
render debug dump scene.hdrColor data/debug/dump/ibl-hdr-color.bmp
render debug dump Forward /game_cam data/debug/dump/ibl-forward.bmp
```

`render debug dump <target> [camera-path] [path]` 由 editor session 转到 Vulkan renderer。若目标是 FrameGraph attachment，可用 `scene.hdrColor` 这样的 attachment 名；若目标是 debug render target pass，可用 pass 名和 camera path。当前不要用 `swapchain.color` 或 cubemap face 作为教程步骤中的验证命令；这些目标需要等 swapchain/cubemap dump 能力落地后再补。

## 常见问题

| 现象 | 优先检查 |
|---|---|
| 金属球发黑 | `test_scene_runtime` 是否通过；PBR draw item 是否收到 `IrradianceMap` / `PrefilteredEnvMap` / `BrdfLut` / `EnvironmentUBO` |
| 画面过曝或过暗 | `PostProcessUBO.exposure`、tone mapping mode、HDR 输入是否仍是线性值 |
| 反射方向不对 | cubemap face orientation；真实 bake 接入后需要 dump cubemap face 对照 HDR 方向 |
| 没有 bloom | `VulkanRenderer::PostProcessSettings::bloomEnabled`、threshold、`bloomIntensity` |
| Headless 环境无法截图 | 使用 `xvfb-run -a`；如果仍失败，按测试输出中的 Vulkan/video device skip 原因排查 |
| 只看到固定 ambient | 检查 scene 是否启用 environment、renderer 是否完成 GPU bake，以及 `PrefilteredEnvMap` 是否绑定 baked mip chain |

## 当前未完成但应该怎样接

| 缺口 | 正确接入点 |
|---|---|
| cubemap / BRDF LUT dump | Vulkan cubemap face / texture dump 可继续扩展为文件化验收 |
| local reflection probe | 独立 requirement，不放进当前场景教程默认能力 |

## 继续阅读

- [材质 Shader 与绑定](../../concepts/material/shader.md)
- [多 Pass 如何变成 Draw](../../concepts/material/pass-rendering-flow.md)
- [FrameGraph](../../concepts-design/rendering-pipeline/framegraph.md)
- [REQ-050-a](../../requirements/050-a-ibl-metal-sphere-test-scene.md)

# 验证与排错：从事实链路定位问题

PBR + IBL 问题通常像摄影棚调试：先确认布景在，再确认灯光资源接上，最后看冲印参数。我们不要只凭一张截图判断，而是按 scene、material、FrameGraph、render dump 的顺序缩小范围。

## 最小验证命令

```bash
cmake --build build --target CompileShaders test_gltf_scene_asset_loader test_render_path_graph_pass_contract test_shader_compiler -j2
./build/src/test/test_gltf_scene_asset_loader
./build/src/test/test_render_path_graph_pass_contract
./build/src/test/test_shader_compiler
```

| 验证 | 说明 |
|---|---|
| glTF scene asset loader | scene/model/material 资源加载链路稳定 |
| render path graph pass contract | Forward/Post/Bloom 的 pass contract 稳定 |
| shader compiler | PBR、post、bloom、IBL bake shader 合同稳定 |

## Editor 与 render dump

从 build 目录启动：

```bash
./build/src/editor/lxe_editor
```

在 Console 中打开 project 内场景后，可以 dump 当前 FrameGraph attachment
或 debug render target pass。HDR attachment 会以调试用 tone mapping 写成 BMP。

```text
render debug dump hdr.color data/debug/dump/ibl-hdr-color.bmp
render debug dump Forward /game_cam data/debug/dump/ibl-forward.bmp
```

`render debug dump <target> [camera-path] [path]` 由 editor session 转到 Vulkan renderer。若目标是 FrameGraph attachment，可用 `hdr.color` 这样的 attachment 名；若目标是 debug render target pass，可用 pass 名和 camera path。当前不要用 `swapchain.color` 或 cubemap face 作为教程步骤中的验证命令；这些目标需要等 swapchain/cubemap dump 能力落地后再补。

## 常见问题

| 现象 | 优先检查 |
|---|---|
| Helmet 发黑 | 先跑 `test_render_resource_parsers` / `test_shader_compiler`；再 dump renderer 目标检查 IBL binding 是否实际上传 |
| 画面过曝或过暗 | `ToneMappingUBO.exposure`、tone mapping mode、HDR 输入是否仍是线性值 |
| 反射方向不对 | cubemap face orientation；真实 bake 接入后需要 dump cubemap face 对照 HDR 方向 |
| 没有 bloom | `BloomUBO.threshold`、`BloomUBO.intensity`、`feature.bloom` 是否进入 ForwardMain |
| Headless 环境无法截图 | 使用 `xvfb-run -a`；如果仍失败，按测试输出中的 Vulkan/video device skip 原因排查 |
| 只看到固定 ambient | 检查 scene 是否启用 environment、renderer 是否完成 GPU bake，以及 `PrefilteredEnvMap` 是否绑定 baked mip chain |

## 当前未完成但应该怎样接

| 缺口 | 正确接入点 |
|---|---|
| cubemap / BRDF LUT dump | Vulkan cubemap face / texture dump 可继续扩展为文件化验收 |
| local reflection probe | 独立 requirement，不放进当前场景教程默认能力 |

## 我们已经学会了什么

我们已经建立了 PBR + IBL 的排错顺序：先用测试确认 scene、material、graph 和 shader 合同，再用 editor dump 查看 `hdr.color` 或 pass 输出，最后才根据截图判断曝光、bloom、反射方向或环境资源问题。这个顺序能避免把资源没绑定、FrameGraph 没接上、tone mapping 参数不对混成同一个“画面不对”。

当前教程验证的是 Helmet 场景的 IBL binding 和 HDR 输出主线。环境 HDR 异步 bake、运行时 IBL lighting 热激活和 local reflection probe 仍然要跟随 active requirements 推进。

## 下一步

- [材质 Shader 与绑定](../../concepts/material/shader.md)
- [多 Pass 如何变成 Draw](../../concepts/material/pass-rendering-flow.md)
- [FrameGraph](../../concepts-design/rendering-pipeline/framegraph.md)
- [PBR + IBL 场景](01-helmet-neutral-ibl-scene.md)
- [REQ-073-g：Environment HDR Async IBL Bake And Runtime Lighting](../../requirements/073-g-environment-hdr-async-ibl-bake-and-runtime-lighting.md)
- [REQ-073-h：Reflection Probe IBL Extension](../../requirements/073-h-reflection-probe-ibl-extension.md)

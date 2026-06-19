# v0.2.0-pre CHANGELOG

> 本页记录 `v0.2.0-pre` 代码基线。发布 tag `v0.2.0-pre` 指向
> `b91a0cd5e093`；后续文档 hard cut 在 main 上单独提交，不移动 tag。

## 发布定位

`v0.2.0-pre` 是 LXEngine 当前渲染与编辑器主线的预发布基线。它把
RenderPathGraph、FrameGraph、RenderWorkCompiler、PBR/IBL、offline compute
和 `src/editor/` 入口固定下来，作为后续功能继续演进的比较点。

## 主要能力

| 领域 | 基线事实 |
|---|---|
| 版本标识 | `LX_PROJECT_VERSION=0.2.0-pre`，BuildInfo 会进入 editor / CLI / 输出 metadata |
| Editor | `lxe_editor` 入口位于 `src/editor/`，默认启动 scene 是 `assets/scenes/generated/helmet_standard_pbr.scene.yaml` |
| Render graph | `assets/render_paths/forward_main.render-path.yaml` 声明 Shadow、Forward、Bloom、DebugOverlay |
| Render work | `FramePass.input` 进入 `RenderWorkCompiler`，backend 消费 `RenderInput[]` 与 `RenderInputDesc[]` |
| PBR/IBL | Damaged Helmet + neutral environment 场景作为当前 IBL 验证对象 |
| Offline | `lxe_offline_render` 读取 scene/profile，走 software-compute integrator 输出 EXR/PNG/JSON/raw readback |
| New track | `LX_BUILD_NEW_TRACK` 默认为 `OFF`，regular track 是当前发布主线 |

## 文档基线

本发布后的 notes hard cut 做了三件事：

- 移除旧 research / review / temp 入口和已删除 render queue 文档。
- 把教程、概念、源码分析和 use case 改到 `src/editor/`、`RenderWorkCompiler`、`hdr.color`、Helmet neutral IBL 当前事实。
- 保留 `notes/requirements/` 作为需求事实来源，但本轮不改需求目录。

## 验证入口

```bash
cmake --build build --target lxe_editor test_shader_compiler -j2
./build/src/test/test_shader_compiler
scripts/notes/serve_site.sh --build
```

涉及 Vulkan/video device 的测试仍按 Linux 环境使用 `xvfb-run -a`。

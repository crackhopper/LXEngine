# Phase 1 · Rendering Depth：0.2.0-pre 后的渲染主线

Phase 1 现在不再是 FrameGraph/Shadow 的立项页；这些已经落到基线里。当前阶段的任务是把已经存在的 RenderPathGraph、FrameGraph、RenderWorkCompiler、PBR/IBL、post/bloom、offline compute 进一步收成可维护的一条主线。

## 当前事实

| 能力 | 当前状态 |
|---|---|
| RenderPathGraph asset | `assets/render_paths/forward_main.render-path.yaml`、deferred/debug/bake graph 已存在 |
| FrameGraph | `compile()` 做 registry-backed source/target validation 和 DAG order |
| RenderWorkCompiler | 生成 scene renderables、fullscreen triangle、compute dispatch 三类 input |
| Forward path | Shadow -> Forward(HDR) -> Bloom -> swapchain/debug overlay |
| RenderFeature | forwardPass、environmentLighting、surfaceLighting、skybox、toneMapping、bloom 都是 asset |
| IBL bake | environment/material bake item、manifest/cache、async job service、FrameGraphExecutor 接口已接入 |
| Offline | `software-compute` integrator 使用 `SceneResourceTableUploadView`、BVH、compute shader、readback |

## 本阶段收口方向

| 方向 | 要解决什么 | 不做什么 |
|---|---|---|
| Backend graph execution | attachment/barrier/executor diagnostics 继续向 graph/executor 集中 | 不恢复旧 queue/item |
| RenderFeature 参数事实源 | 减少 C++ helper 手写参数，统一从 feature asset/scene resource table 注入 | 不让 scene legacy 字段绕过 feature |
| PBR/IBL 验证 | Helmet、neutral IBL、bake manifest/cache 形成稳定验收集 | 不一次性做完整 probe system |
| OfflineRT graph hard cut | 让 offline compute pass 更自然地来自 RenderPathGraph/FrameGraph | 不在本阶段承诺硬件 RT |
| Deferred / post | 保持 graph asset 可解析和测试覆盖，逐步补齐运行时质量 | 不把实验 graph 讲成已完成 renderer |

## 验收形态

| 验收 | 说明 |
|---|---|
| `test_render_resource_parsers` | graph、feature、material、IBL bake 合同保持同步 |
| `test_shader_compiler` | shader reflection 与 feature ABI 没漂移 |
| `test_frame_graph_executor` | IBL bake job service 和 executor interface 保持独立 |
| `lxe_editor` PBR/IBL use case | editor 能打开当前模板并 dump HDR target |
| `lxe_offline_render` smoke | headless scene/profile/output 链路可复现 |

## 继续阅读

- [渲染管线](../../concepts-design/rendering-pipeline/index.md)
- [RenderWorkCompiler](../../concepts-design/rendering-pipeline/render-work-compiler.md)
- [PBR + IBL 教程](../../tutorial/pbr-ibl/index.md)
- [Offline Renderer 教程](../../tutorial/offline-renderer/index.md)

# Offline Renderer 实现结构：从 Scene 到 FrameGraphExecutor

Offline renderer 像一条离线实验流水线：editor 产出场景说明书，loader 把说明书整理进 `SceneResourceTable`，output profile 选择 render path graph，`RenderWorkCompiler` 把 pass 准备成 draw/dispatch work，Vulkan `FrameGraphExecutor` 统一执行，image writer 把 readback payload 保存成可复现记录。

## 总体流水线

| 阶段 | 代码入口 | 输入 | 输出 |
|---|---|---|---|
| CLI 参数 | `src/tools/lxe_offline_render/main.cpp` | `--scene` / `--profile` / overrides | 解析后的 scene/profile/output 请求 |
| Scene 读取 | `src/infra/scene_io/scene_document.*` | `.scene.yaml` | `SceneDocument` |
| Profile 选择 | `src/core/offline/offline_render_profile.*` | `scene.outputProfiles` + CLI overrides | `ResolvedRenderProfile` |
| Scene 加载 | `src/infra/offline/offline_scene_loader.*` | `SceneDocument` | `Scene` + `SceneResourceTable` |
| Graph 读取 | `OutputProfile.renderPathGraph` | `.render-path.yaml` | `RenderPathGraph` / `FrameGraph` |
| Work 编译 | `src/core/frame_graph/render_work_compiler.*` | compiled graph + scene/resource/material/feature facts | prepared pass work + `RenderInputDesc` |
| 执行 | `src/backend/vulkan/vulkan_frame_graph_executor.*` | prepared work + targets + readback contract | `FrameGraphExecutionPayload` |
| 文件写出 | `src/infra/offline/offline_image_writer.*` | linear RGBA payload | EXR / PNG / JSON / raw |

这条流水线的关键是：offline 不再有独立 public job graph。它和 realtime 共用 scene、resource table、render feature、material、render path graph、pipeline cache、descriptor 和 command buffer 基础设施；差异由 output profile 选择的 graph 和 executor domain 表达。

## CLI 只编排，不渲染

`src/tools/lxe_offline_render/main.cpp` 做四件事：

1. 解析命令行参数。
2. 读取 scene 并解析 output profile。
3. 加载 scene/resource table，并把 profile 指定的 render path graph 交给 renderer。
4. 调用 `writeOfflineImageOutputs()` 写文件。

它不直接写 EXR，也不直接创建 pass、descriptor 或 pipeline。这个边界让测试、批处理和未来 editor 离线渲染入口可以复用同一条 renderer/writer 路径。

## 一个 Scene，多个 Render Path Graph

当前对比场景固定为：

```text
assets/scenes/generated/helmet_standard_pbr.scene.yaml
```

它通过 output profile 选择不同 graph：

| Profile | Graph | 用途 |
|---|---|---|
| `forward_no_ibl` | `assets/render_paths/forward_offline_direct.render-path.yaml` | Forward 直射光，不启用 IBL |
| `ibl_only` | `assets/render_paths/forward_offline_ibl_only.render-path.yaml` | Forward 只看 IBL |
| `forward_ibl` | `assets/render_paths/forward_offline_direct_ibl.render-path.yaml` | Forward 直射光 + IBL |
| `raytrace` | `assets/render_paths/offline_standard_pbr_raytrace.render-path.yaml` | OfflineRT primary ray + direct lighting + miss environment |

这意味着渲染流程差异在 render path graph 里表达，而不是复制 scene 或改 object/camera/light。scene 负责描述对象、相机、灯光、材质和 skybox；profile/graph 负责描述这次要跑哪套渲染流程。

## SceneDocument 到 SceneResourceTable

`OfflineSceneLoader` 把 scene 文档加载进统一的 scene/resource 模型。它不再为 offline 创建一套并行对象系统，而是复用实时流程中已经验证过的资源表和材质/feature 解析。

| Scene 文档信息 | 进入的运行时事实 |
|---|---|
| selected `OutputProfile.camera` | active camera resource |
| mesh node + transform | object/mesh/material resource |
| material URI / parameters | `MaterialInstance` |
| directional light | light resource |
| `skybox.mode: finite` | 普通 mesh/material 节点，参与 graph filter 后绘制 |
| `skybox.mode: infinite` | 注册 environment lighting feature 和 runtime state |

finite skybox 是场景几何；infinite skybox 是场景级环境。这个拆分让 Forward 和 OfflineRT 都能用同一份 scene：Forward 可以把 finite 当普通 mesh 画，也可以用 environment lighting 做 IBL；OfflineRT 在 ray miss 时采样 infinite environment。

## RenderWorkCompiler 准备 Pass Work

`RenderWorkCompiler` 的职责是把 graph pass 翻译为可执行 work：

| 输入事实 | 用途 |
|---|---|
| compiled graph pass | pass 顺序、读写资源、target/readback 声明 |
| `Scene` / `SceneResourceTable` | scene renderables、resource table snapshot、light/camera/material |
| render feature | shader 参数、runtime value、derived resource、pipeline extra |
| material | shader variant、descriptor resource、参数布局 |
| output profile | target extent、readback format、camera override |

对 raster pass，它生成 draw work 和 graphics `RenderInputDesc`；对 compute pass，它生成 dispatch work 和 compute `RenderInputDesc`。readback 不是 pass，它是 graph/output contract：executor 在 pass 执行后按 contract 把 attachment 或 storage buffer 复制成 `FrameGraphExecutionPayload`。

## BVH 和 Ray Program Table 的位置

Software BVH 被建模为 render feature 的 derived resource：feature 声明需要 BVH，准备阶段根据 scene/resource table 构建 CPU/GPU 数据，并把它加入 descriptor 与 pipeline build extra。未来切到 hardware RT 时，同一类 feature 可以改成构建 BLAS/TLAS 和 shader table，而不是让 OfflineRT 用户层重写 graph。

Ray program table 也是 render feature：它描述 hit group index 与 hit shader 入口的关系。软件 RT 里统一 hit 入口用 switch/if 调用 material hit shader；hardware RT 里同一份 table 可以协助构建 hit shader table。具体 hit shader 属于 material scheme，不属于 render feature。

## FrameGraphExecutor 执行和 Readback

Vulkan `FrameGraphExecutor` 负责：

| 步骤 | 说明 |
|---|---|
| target 创建 | 根据 graph target/write contract 创建 offscreen attachment |
| resource sync | 上传 pass 依赖的 buffer/image/material/feature resource |
| pipeline 准备 | 使用 `RenderInputDesc` 构建或复用 graphics/compute pipeline |
| pass 执行 | raster pass 走 dynamic rendering，compute pass 走 dispatch |
| layout/barrier | 将写入资源转成后续 pass/readback 可读状态 |
| readback | 按 readback contract 复制 attachment 或 buffer |

IBL bake、Forward offline 对比和 OfflineRT 都走这条 executor 路径。IBL bake 在 readback 后仍然可以追加自己的文件写入或资源注册流程，但 readback 本身不再是 bake 专属机制。

## Image Writer 是输出模块

`OfflineImageWriter` 接收 executor payload，转换为 `OfflineReadbackImage`，再写出：

| 输出 | 实现 |
|---|---|
| EXR | TinyEXR，scene-linear RGBA half/float |
| PNG | stb_image_write，CPU ACES tone mapping + gamma |
| JSON | sidecar metadata |
| RGBA32F | 原始 float buffer，调试用 |

EXR/PNG 的具体编码只出现在 writer 和底层 image IO 实现里；core、scene loader、render work compiler 和 executor 都不需要理解图像库细节。

## 当前可扩展点

| 想扩展 | 入口 |
|---|---|
| 增加新的对比输出 | 在 scene 增加 output profile，并指向新的 render path graph |
| 增加 pass readback | 在 graph/output contract 里声明 readback，不新增 readback pass |
| 增加 skybox 资产形态 | 扩展 scene `skybox` schema 和 environment feature |
| 切换硬件 RT | 保持 graph/profile，替换 BVH feature derived resource 和 ray program table 后端 |
| 增加 AOV 输出 | 增加 target/readback contract 和 writer 对 payload 的选择 |

## 我们已经学会了什么

我们已经把 offline renderer 拆成了八段：CLI、scene document、output profile、scene loader、render path graph、RenderWorkCompiler、FrameGraphExecutor、image writer。每一段都有清晰输入输出，所以后续实现 path tracing 或 hardware RT 时，可以明确知道应该改哪一层。

## 下一步

继续读 [源码阅读路线](04-code-reading-guide.md)，我们会把上面的流水线落实到具体文件、函数和测试，再进入 [实现自己的 Path Tracing](05-implement-path-tracing.md)。

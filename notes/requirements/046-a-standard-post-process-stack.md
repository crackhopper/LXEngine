# REQ-046-a: Standard Post-process Stack

> 2026-05-26 新增：本 REQ 建立标准 HDR 后处理主线。目标是把当前 Forward 直接写 swapchain 的路径改为 `scene.hdrColor -> post-process -> swapchain`，并清理旧的 fullscreen procedural 分支。

## 背景

当前 Vulkan renderer 在 `initScene()` 中建立 shadow passes、`Pass_Forward` 和 `Pass_DebugOverlay`。`Pass_Forward` 直接写 `swapchain.color` / `swapchain.depth`，ImGui overlay 在最终 swapchain render pass 内提交。`FramePassKind::FullscreenProcedural` 和 `FramePass::fullscreenMaterial` 已存在于 core FrameGraph，但 backend 实际 fullscreen draw executor 还未完成，`REQ-045-c` 也明确写到 fullscreen procedural 只完成了 core 表达。

PBR + IBL 需要 HDR 中间结果。若继续在 PBR shader 内做 tone mapping，会把曝光、bloom、gamma 和最终显示混进材质 shader，后续无法形成标准渲染管线。

外部参考：

| 参考 | 对本 REQ 的启发 |
|---|---|
| Filament PBR 文档 | PBR 与 IBL 输出应进入统一 imaging pipeline，而不是在单个材质内收口 |
| Godot Environment/Post-processing 文档 | 背景、环境光、反射光和后处理属于场景级渲染配置 |

## 目标

1. 引入标准 `Pass_PostProcess`。
2. 让 Forward 写 HDR offscreen color，Post 写 swapchain。
3. 提供 tone mapping、gamma、exposure 和 bloom 的第一版能力。
4. 把艺术实验 fullscreen 路径迁移到标准 post-process stack。
5. 删除旧的 `FullscreenProcedural` 兼容路径，不保留并行系统。

## 需求

### R1: 新增 PostProcess pass 身份

新增 `Pass_PostProcess`，与 `Pass_Forward` / `Pass_Shadow` / `Pass_DebugOverlay` 同级。

要求：

- `Pass_PostProcess` 是最终写 swapchain color 的标准 pass。
- `Pass_DebugOverlay` 和 ImGui overlay 位于 post-process 之后。
- FrameGraph 顺序默认为：

```text
Shadow -> Forward(HDR offscreen) -> PostProcess(swapchain) -> DebugOverlay/ImGui
```

### R2: Forward 输出 HDR scene color

`VulkanRenderer::Impl::initScene()` 不再让 Forward 直接写 swapchain color。

要求：

- Forward target 使用 offscreen color + depth。
- Forward 写出 `scene.hdrColor` 和 `scene.depth`。
- Forward 仍读取 shadow cascades。
- swapchain target 只由 PostProcess / overlay 路径写入。

### R3: Standard fullscreen post executor

新增标准 fullscreen executor，用于执行 post-process pass。

要求：

- 不依赖 scene node、mesh component 或 transform。
- 使用固定 fullscreen triangle 或等价 backend draw。
- 支持 material-style reflected parameters 和 sampled inputs。
- 支持读取 `FrameGraphSampledResource("scene.hdrColor", "SceneColor")`。
- 生成可缓存的 pipeline build desc。

### R4: Tone mapping 与 gamma

提供最小标准 tone mapping pass。

要求：

- 支持 exposure 参数。
- 至少支持 `ACES` 和 `Reinhard` 两种 tone mapping mode。
- 支持最终 sRGB/gamma correction。
- tone mapping 在 post shader 中执行，不允许 PBR material shader 自行完成最终显示映射。

### R5: Bloom 基础链路

提供 bloom 的第一版 pass 编排。

要求：

- 支持 threshold 提取。
- 支持至少一层 downsample / upsample 或等价 blur-composite 路径。
- 支持 intensity 参数。
- bloom 可以作为 post stack 配置项关闭。
- v1 不要求 artist-facing UI，但配置必须能被 scene/demo 入口稳定设置。

### R6: 清理旧 fullscreen procedural 路径

迁移并删除旧的 fullscreen procedural 表达。

要求：

- 移除 `FramePassKind::FullscreenProcedural`。
- 移除 `FramePass::fullscreenMaterial` 和 `CompiledFrameGraphPass::fullscreenMaterial`。
- `REQ-045-c` 中 procedural fullscreen 的后续实现入口迁移到标准 post-process pass。
- 不提供旧字段兼容读取或 runtime fallback。

### R7: 测试覆盖

至少覆盖：

- FrameGraph 能表达 Forward 写 `scene.hdrColor`、PostProcess 读 `scene.hdrColor` 并写 swapchain。
- renderer 编译后的 pass 顺序符合 R1。
- post fullscreen pipeline build desc 可生成。
- tone mapping shader 编译并反射出 exposure / mode 所需参数。
- 代码搜索不再出现 `FullscreenProcedural` 旧入口。
- 无 GUI 截图或 framebuffer dump 能验证 tone mapping 输出不是未映射 HDR 值。

## 修改范围

- `src/core/frame_graph/`
- `src/core/frame_graph/pass.*`
- `src/backend/vulkan/vulkan_renderer.cpp`
- `src/backend/vulkan/details/commands/`
- `src/backend/vulkan/details/pipelines/`
- `assets/shaders/glsl/`
- `assets/materials/`
- `src/test/integration/`
- `notes/requirements/045-c-procedural-audio-and-framegraph-integration.md`（只更新状态/后续入口说明）

## 边界与约束

- 本 REQ 不实现 IBL bake。
- 本 REQ 不实现 PBR shader 改造。
- 本 REQ 不实现 editor UI 调参面板。
- 本 REQ 不保留旧 fullscreen procedural 兼容分支。
- 本 REQ 不引入自动 FrameGraph reorder。

## 依赖

- `REQ-045-c`：已有 procedural fullscreen 的未完成表达，需要迁移。
- `openspec/specs/frame-graph/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/material-system/spec.md`

## 后续工作

- `REQ-047-a`：HDR / cubemap resource 形状。
- `REQ-048-a`：IBL GPU bake pipeline。
- `REQ-049-a`：PBR + IBL material contract。

## 实施状态

实施中。

已落地：

- 新增 `Pass_PostProcess` 作为 core pass 身份。
- Core FrameGraph 已能表达 `Forward(RGBA16Float scene.hdrColor) -> PostProcess(SceneColor sampled read) -> swapchain.color` 资源流。
- 已删除 `FramePassKind::FullscreenProcedural`、`FramePass::fullscreenMaterial` 和 `CompiledFrameGraphPass::fullscreenMaterial` 旧字段。
- `REQ-045-c` 已迁移说明：旧 fullscreen procedural 分支不继续扩展，后续统一走标准 post-process stack。
- Vulkan backend 已具备 `RGBA16Float` render-target format 映射，debug render-target pass name 也能解析 `PostProcess`。

仍待落地：

- Forward HDR offscreen target 与 Vulkan renderer pass 顺序迁移。
- 标准 fullscreen post executor、tone mapping、gamma 与 bloom。
- post shader/material、pipeline build desc 与截图/framebuffer 验证。

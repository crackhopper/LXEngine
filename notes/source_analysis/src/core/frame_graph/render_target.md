# RenderTarget：attachment 形状如何成为 REQ-009 的匹配键

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/frame_graph/render_target.hpp](../../../../../src/core/frame_graph/render_target.hpp)
出发，关注的不是"它有哪几个字段"，而是：为什么 `RenderTarget` 被刻意做成
一个不持有句柄、不直接拥有 backend image 的薄 POD，以及它怎么作为 camera
选择和 pass attachment 合同里的 *target 轴* 在 Scene、FramePass、
RenderWorkCompiler 和 backend 之间穿过。

可以先带着一个问题阅读：既然 backend 最终要的是 attachment 句柄，为什么
`RenderTarget` 不直接持有句柄？答案是，句柄随 swapchain 重建抖动，而
"camera 匹配哪个 target" 是配置层的事实 — 把这两件事捏在一起会让 REQ-009
的匹配判断跟着 backend 状态一起抖。

源码入口：[render_target.hpp](../../../../src/core/frame_graph/render_target.hpp)

## RenderTarget：旧 target 轴的兼容外壳

`FramePass` 已经用 `RenderTargetDesc` 保存完整 target 形状，但当前 scene/camera
筛选接口仍接收 `RenderTarget`。因此 `RenderTarget` 现在是兼容外壳：旧的
`colorFormat` / `depthFormat` / `sampleCount` 字段仍是 present attachment 的
格式来源，额外的 presence bit、role 和 layerCount 只补足旧结构表达不了的语义。
这样既有代码直接写 `target.colorFormat` / `target.depthFormat` 时不会被 `toDesc`
忽略，offscreen/depth-only target 也不会被误还原成默认 swapchain target。

## operator==：按完整描述比较 target 轴

`RenderTarget::operator==` 被 `Camera::matchesTarget` 用作 target 轴判定。比较
必须走 `toDesc()`，否则从 `RenderTargetDesc` 降级到兼容类型时会丢失
offscreen/depth-only 的 role、attachment presence 和 layerCount 语义。

<!-- SOURCE_ANALYSIS:EXTRA -->

## target 轴的真相

当前实现有三处依赖 target 轴：

| 位置 | 目的 | 当前真实状态 |
|------|------|------------------|
| `FramePass::target` | 保存 pass 输出 attachment 形状 | 来自 RenderPathGraph 的 rendering/attachments 合同 |
| `Scene::getSceneLevelResources(pass, target)` | 为 compiler 取 scene-level camera/light 资源 | camera 按 target 匹配，light 按 pass 匹配 |
| `RenderWorkCompiler::buildInputs(...)` / `prepare(...)` | 把 pass target 带入 input 筛选、binding plan 和 pipeline build desc | 不持有 backend image 句柄 |

也就是说 target 轴已经是配置层形状匹配，不是 backend attachment 句柄匹配。

## 当前边界

| 层 | 当前职责 |
|---|---|
| `RenderTargetDesc` | pass 输出形状和 pipeline build input |
| `RenderTarget` | scene/camera target 匹配的兼容外壳 |
| backend attachment | 真实 image/framebuffer/render pass 生命周期 |
| `PipelineKey` | 不再单独包含 target 轴；target/attachment contract 通过 RenderPathNode signature 进入 key |

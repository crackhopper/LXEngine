# 源码分析

这个目录放的是“贴着真实源码读”的笔记。和 `notes/concepts/` 的高层概念与设计不同，它不试图先讲完整主题，再落到代码；这里更关心某个头文件、某个类、某组实现细节为什么会这样组织。

我们把这套笔记做成了一个轻量的 literate workflow：

| 部分 | 放在哪里 | 作用 |
|------|-----------|------|
| 结构性讲解 | 源码里的 `@source_analysis.section` 注释块 | 让讲解直接附着在类型和实现边界上 |
| 自动抽取 | `scripts/source_analysis/extract_sections.py` | 把源码中的分析块整理成 Markdown |
| 扩展说明 | `notes/source_analysis/*.md` 的 `SOURCE_ANALYSIS:EXTRA` 之后 | 补充源码之外的上下文、示意图、对比说明 |

这样做的目的不是替代普通注释，而是把两类信息分开：

- 源码里只保留和当前声明强绑定的解释，避免文档飘离代码
- Markdown 里可以展开讲背景、取舍和阅读顺序，避免头文件被大段说明淹没

目录布局也故意按源码相对路径镜像，例如：

- 源码：`src/core/asset/material_instance.hpp`
- 分析：`notes/source_analysis/src/core/asset/material_instance.md`

这样我们在浏览 `notes/source_analysis/` 时，能直接沿着源码目录去找对应分析，不需要再维护一套独立命名体系。

## 建议阅读路径

源码分析更像按源码目录摆放的剖面图。第一次读时，我们可以按“数据结构先行、渲染流随后、最后进入 backend/offline”的顺序：

| 顺序 | 关注点 | 页面 |
|---|---|---|
| 1 | GPU 资源最小公共契约 | [IGpuResource](src/core/rhi/gpu_resource.md) |
| 2 | asset / material / texture 如何变成渲染输入 | [Mesh](src/core/asset/mesh.md)、[Shader](src/core/asset/shader.md)、[MaterialTemplate](src/core/asset/material_template.md)、[MaterialInstance](src/core/asset/material_instance.md)、[Texture](src/core/asset/texture.md) |
| 3 | scene 与 frame graph 怎样收口 draw item | [Scene](src/core/scene/scene.md)、[RenderTarget](src/core/frame_graph/render_target.md)、[RenderQueue](src/core/frame_graph/render_queue.md)、[FrameGraph](src/core/frame_graph/frame_graph.md) |
| 4 | pipeline identity 为什么只看结构差异 | [Pipeline Identity](src/core/pipeline/pipeline_identity.md) |
| 5 | 离线渲染如何从 scene IR 进入 Vulkan compute | [Vulkan Offline Renderer](src/backend/vulkan/offline/vulkan_offline_renderer.md) |

当前入口：

- [IGpuResource：core 层的 GPU 资源统一契约](src/core/rhi/gpu_resource.md)
- [Mesh：几何接口形状如何进入渲染签名](src/core/asset/mesh.md)
- [IShader & ShaderResourceBinding：反射结果如何落地到材质系统](src/core/asset/shader.md)
- [MaterialTemplate：多 pass 蓝图如何收束成统一契约](src/core/asset/material_template.md)
- [MaterialInstance：从模板到运行时账本](src/core/asset/material_instance.md)
- [Texture 与 CombinedTextureSampler：CPU 图像如何进入 GPU 资源路径](src/core/asset/texture.md)
- [Scene：场景容器与 scene-level 资源筛选](src/core/scene/scene.md)
- [RenderTarget：attachment 形状如何成为 target 匹配键](src/core/frame_graph/render_target.md)
- [RenderQueue：把 scene × pass 收口成可消费的 draw 列表](src/core/frame_graph/render_queue.md)
- [FrameGraph：把 scene 翻译成按 pass 组织的 RenderingItem 列表](src/core/frame_graph/frame_graph.md)
- [GlobalStringTable：字符串驻留与结构化身份树](src/core/utils/string_table.md)
- [Pipeline Identity：从结构签名到构建输入](src/core/pipeline/pipeline_identity.md)
- [Vulkan Offline Renderer：从 Scene IR 到 Compute Readback](src/backend/vulkan/offline/vulkan_offline_renderer.md)

配套阅读：

- [MaterialInstance：运行时状态](../concepts/material/material-instance.md)：先建立材质实例的概念，再回到源码分析看实现边界
- [多 Pass 材质怎样变成 Draw](../concepts/material/pass-rendering-flow.md)：先理解 queue / item / pass 的高层流向，再读 RenderQueue / FrameGraph 源码分析

## 更新方式

1. 修改源码里的 `@source_analysis.section` 注释块
2. 运行 `python3 scripts/source_analysis/extract_sections.py`
3. 如有需要，再补 `<!-- SOURCE_ANALYSIS:EXTRA -->` 后面的扩展内容

当前脚本只会生成 `TARGETS` 里显式列出的页面，不会对整个 `src/` 自动扫全量分析页。这一层配置就是“只分析部分源码”的开关。源码分析导航顺序也由 target 元数据控制，而不是在 `notes/nav.yml` 里逐页手写。

## 继续阅读

- [MaterialInstance：运行时状态](../concepts/material/material-instance.md)
- [多 Pass 材质怎样变成 Draw](../concepts/material/pass-rendering-flow.md)
- [Offline Renderer 教程](../tutorial/offline-renderer/index.md)

# 源码分析

这个目录放的是“贴着真实源码读”的笔记。和 `notes/subsystems/` 相比，它不试图先讲完整子系统，再落到代码；这里更关心某个头文件、某个类、某组实现细节为什么会这样组织。

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

当前入口：

- [IGpuResource：core 层的 GPU 资源统一契约](src/core/rhi/gpu_resource.md)
- [Mesh：几何接口形状如何进入渲染签名](src/core/asset/mesh.md)
- [IShader & ShaderResourceBinding：反射结果如何落地到材质系统](src/core/asset/shader.md)
- [MaterialTemplate：多 pass 蓝图如何收束成统一契约](src/core/asset/material_template.md)
- [MaterialInstance：从模板到运行时账本](src/core/asset/material_instance.md)
- [Texture 与 CombinedTextureSampler：CPU 图像如何进入 GPU 资源路径](src/core/asset/texture.md)
- [Scene：场景容器与 scene-level 资源筛选](src/core/scene/scene.md)
- [RenderTarget：attachment 形状如何成为 REQ-009 的匹配键](src/core/frame_graph/render_target.md)
- [RenderQueue：把 scene × pass 收口成可消费的 draw 列表](src/core/frame_graph/render_queue.md)
- [FrameGraph：把 scene 翻译成按 pass 组织的 RenderingItem 列表](src/core/frame_graph/frame_graph.md)

配套阅读：

- [资源上传](../subsystems/resource-upload.md)：解释 `IGpuResource` 进入 backend 后，什么时候真正上传、由谁触发、buffer / texture 路径怎样分流

## 更新方式

1. 修改源码里的 `@source_analysis.section` 注释块
2. 运行 `python3 scripts/source_analysis/extract_sections.py`
3. 如有需要，再补 `<!-- SOURCE_ANALYSIS:EXTRA -->` 后面的扩展内容

当前脚本只会生成 `TARGETS` 里显式列出的页面，不会对整个 `src/` 自动扫全量分析页。这一层配置就是“只分析部分源码”的开关。源码分析导航顺序也由 target 元数据控制，而不是在 `notes/nav.yml` 里逐页手写。

## 继续阅读

- [材质系统](../subsystems/material-system.md)
- [MaterialInstance：运行时状态](../concepts/material/material-instance.md)
- [资源上传](../subsystems/resource-upload.md)

#!/usr/bin/env python3
"""Extract lightweight literate source-analysis blocks into notes/.

Convention in source files:

/*
@source_analysis.section Section Title
Markdown body...
*/

The generated markdown contains an auto-generated section and preserves any
manual content placed after `<!-- SOURCE_ANALYSIS:EXTRA -->`.
"""

from __future__ import annotations

import re
import textwrap
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
EXTRA_MARKER = "<!-- SOURCE_ANALYSIS:EXTRA -->"
SECTION_RE = re.compile(
    r"/\*\s*\n@source_analysis\.section[ \t]+(?P<title>[^\n]+)\n(?P<body>.*?)\*/",
    re.DOTALL,
)


@dataclass(frozen=True)
class SourceAnalysisTarget:
    source: str
    output: str
    title: str
    intro: str
    related_sources: tuple[str, ...] = ()
    nav_order: int = 1000

    @property
    def all_sources(self) -> tuple[str, ...]:
        return (self.source, *self.related_sources)


TARGETS = [
    SourceAnalysisTarget(
        source="src/core/rhi/gpu_resource.hpp",
        output="notes/source_analysis/src/core/rhi/gpu_resource.md",
        title="IGpuResource：core 层的 GPU 资源统一契约",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/rhi/gpu_resource.hpp](../../../../../src/core/rhi/gpu_resource.hpp)
            出发，解释为什么引擎需要一个极薄的 `IGpuResource` 接口，
            以及为什么 `CameraData`、`SkeletonData`、`ParameterBuffer`、
            `CombinedTextureSampler`、`VertexBuffer` / `IndexBuffer`
            这些看起来语义不同的类型会在这里汇合。

            可以先带着一个问题阅读：为什么项目没有把“CPU 侧对象”和
            “backend 侧 upload/bind 输入”拆成更多层？答案是，这里故意只保留
            backend 真正需要的最小公共契约，避免每条资源路径都发明一套专用接口。
            """
        ).strip(),
        nav_order=100,
    ),
    SourceAnalysisTarget(
        source="src/core/asset/mesh.hpp",
        output="notes/source_analysis/src/core/asset/mesh.md",
        title="Mesh：几何接口形状如何进入渲染签名",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/asset/mesh.hpp](../../../../../src/core/asset/mesh.hpp)
            出发，但真正想回答的问题不是“Mesh 里有哪些字段”，而是：
            为什么项目把几何对象做得这么薄，却仍然能让 pipeline、scene 校验和 backend
            都拿到自己需要的结构事实。

            可以先带着一个问题阅读：`Mesh` 为什么没有直接保存材质、draw state，
            却还能参与 `PipelineKey`？答案是，这里真正进入渲染签名的不是“几何内容本身”，
            而是顶点输入布局和图元拓扑这两类几何接口形状。
            """
        ).strip(),
        related_sources=(
            "src/core/rhi/vertex_buffer.hpp",
            "src/core/rhi/index_buffer.hpp",
        ),
        nav_order=200,
    ),
    SourceAnalysisTarget(
        source="src/core/asset/shader.hpp",
        output="notes/source_analysis/src/core/asset/shader.md",
        title="IShader & ShaderResourceBinding：反射结果如何落地到材质系统",
        intro=textwrap.dedent(
            """\
            这一页把反射层当作一条独立的读路径来读，入口是
            [src/core/asset/shader.hpp](../../../../../src/core/asset/shader.hpp)。
            关注的问题是：shader 反射出来的 binding 列表，是怎么被切开成
            “引擎全局数据”和“材质自己管理的资源”这两部分，交给材质系统的。

            顺着 `ShaderResourceBinding` → `IShader::getReflectionBindings()` →
            `shader_binding_ownership.hpp` 这条线读，可以回答：为什么材质系统不是
            直接按 shader 反射结果分配 buffer，而是先经过一道按名字分流的 ownership
            过滤，才开始构造 canonical material binding 表。
            """
        ).strip(),
        related_sources=(
            "src/core/asset/shader_binding_ownership.hpp",
        ),
        nav_order=250,
    ),
    SourceAnalysisTarget(
        source="src/core/asset/material_instance.hpp",
        output="notes/source_analysis/src/core/asset/material_instance.md",
        title="MaterialInstance：从模板到运行时账本",
        intro=textwrap.dedent(
            """\
            这一页不是重新抄一遍 API，而是顺着
            [src/core/asset/material_instance.hpp](../../../../../src/core/asset/material_instance.hpp)
            和
            [src/core/asset/parameter_buffer.hpp](../../../../../src/core/asset/parameter_buffer.hpp)
            的类型定义来看：这个类为什么要拆成 `ParameterBuffer`
            和 `MaterialInstance` 两层，以及为什么它现在坚持
            “按 canonical material bindings 分组的运行时资源表 + pass 级只读筛选”这条主线。

            可以先带着一个问题阅读：`MaterialTemplate` 已经定义了 pass 和 shader，
            为什么还需要一个 `MaterialInstance`？答案是，template 负责“结构是什么”，
            instance 负责“这一帧真正要喂给 backend 的实例数据是什么”。
            """
        ).strip(),
        related_sources=(
            "src/core/asset/parameter_buffer.hpp",
        ),
        nav_order=310,
    ),
    SourceAnalysisTarget(
        source="src/core/asset/material_template.hpp",
        output="notes/source_analysis/src/core/asset/material_template.md",
        title="MaterialTemplate：多 pass 蓝图如何收束成统一契约",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/asset/material_template.hpp](../../../../../src/core/asset/material_template.hpp)
            出发，关注的不是 API 清单，而是这个类型如何把多个 pass 的 shader、
            render state 和 material-owned binding 收束成一份可共享的蓝图。

            可以先带着一个问题阅读：为什么 `MaterialTemplate` 既要保留 per-pass 定义，
            又要额外构建 canonical material binding 表？答案是，前者回答“这个材质
            在结构上有哪些 pass”，后者回答“这些 pass 共同引用的是哪一组稳定 binding 契约”。
            """
        ).strip(),
        related_sources=(
            "src/core/asset/material_pass_definition.hpp",
            "src/core/asset/shader.hpp",
        ),
        nav_order=300,
    ),
    SourceAnalysisTarget(
        source="src/core/scene/scene.hpp",
        output="notes/source_analysis/src/core/scene/scene.md",
        title="Scene：场景容器与 scene-level 资源筛选",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/scene/scene.hpp](../../../../../src/core/scene/scene.hpp)
            和它的实现
            [src/core/scene/scene.cpp](../../../../../src/core/scene/scene.cpp)
            出发，关注的不是 API 列表，而是 `Scene` 为什么是一层薄壳：
            把结构验证下放给 `SceneNode`、把 draw 组装下放给 `RenderQueue`，
            自己只保留 nodeName 唯一性、shared material 重验证传播、
            以及 scene-level 资源的两轴筛选这三件无法下放的事情。

            可以先带着一个问题阅读：为什么 `Scene` 的容器是平铺的、构造时还要硬塞
            一个默认 Camera 和 DirectionalLight？答案是 REQ-009 — 让那些不走完整
            `VulkanRenderer::initScene` 的纯 core/test 路径仍然能拿到非空的
            scene-level 资源，同时把 hierarchy/可见性等可选维度整体下推给 SceneNode。
            """
        ).strip(),
        related_sources=(
            "src/core/scene/scene.cpp",
        ),
        nav_order=500,
    ),
    SourceAnalysisTarget(
        source="src/core/frame_graph/render_target.hpp",
        output="notes/source_analysis/src/core/frame_graph/render_target.md",
        title="RenderTarget：attachment 形状如何成为 REQ-009 的匹配键",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/frame_graph/render_target.hpp](../../../../../src/core/frame_graph/render_target.hpp)
            和它的实现
            [src/core/frame_graph/render_target.cpp](../../../../../src/core/frame_graph/render_target.cpp)
            出发，关注的不是"它有哪几个字段"，而是：为什么 `RenderTarget` 被刻意做成
            一个不持有句柄、不参与 PipelineKey 的薄 POD，以及它怎么作为 REQ-009 两轴
            筛选里的 *target 轴* 在 Scene、Camera、RenderQueue 之间穿过。

            可以先带着一个问题阅读：既然 backend 最终要的是 attachment 句柄，为什么
            `RenderTarget` 不直接持有句柄？答案是，句柄随 swapchain 重建抖动，而
            "camera 匹配哪个 target" 是配置层的事实 — 把这两件事捏在一起会让 REQ-009
            的匹配判断跟着 backend 状态一起抖。
            """
        ).strip(),
        related_sources=(
            "src/core/frame_graph/render_target.cpp",
        ),
        nav_order=550,
    ),
    SourceAnalysisTarget(
        source="src/core/frame_graph/render_queue.hpp",
        output="notes/source_analysis/src/core/frame_graph/render_queue.md",
        title="RenderQueue：把 scene × pass 收口成可消费的 draw 列表",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/frame_graph/render_queue.hpp](../../../../../src/core/frame_graph/render_queue.hpp)
            和它的实现
            [src/core/frame_graph/render_queue.cpp](../../../../../src/core/frame_graph/render_queue.cpp)
            出发，关注的不是"队列里有哪些 API"，而是 `RenderQueue` 在数据流里的位置：
            它是 per-pass 的 RenderingItem 收口点，把 `Scene` 的扁平容器视角翻译成
            backend 能直接消费的 draw 列表。

            可以先带着一个问题阅读：为什么 `RenderQueue` 不是一个全局队列？答案是，
            同一个 renderable 在不同 pass 下的 RenderingItem 本就不同（不同 shader、
            不同 binding、不同 pipelineKey），合并会让"按 pipelineKey 聚合"的语义崩塌；
            这里只在单个 pass 内部完成 REQ-009 两轴筛选、稳定排序、和 pipeline 去重。
            """
        ).strip(),
        related_sources=(
            "src/core/frame_graph/render_queue.cpp",
        ),
        nav_order=600,
    ),
    SourceAnalysisTarget(
        source="src/core/frame_graph/frame_graph.hpp",
        output="notes/source_analysis/src/core/frame_graph/frame_graph.md",
        title="FrameGraph：把 scene 翻译成按 pass 组织的 RenderingItem 列表",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/frame_graph/frame_graph.hpp](../../../../../src/core/frame_graph/frame_graph.hpp)
            和它的实现
            [src/core/frame_graph/frame_graph.cpp](../../../../../src/core/frame_graph/frame_graph.cpp)
            出发，关注的不是"FrameGraph 有哪些方法"，而是它在 frame_graph 子系统里的
            位置：FramePass 把 (name, target, queue) 三件强绑定的字段打包成一条 pass 的
            完整描述，FrameGraph 在加载期把每条 pass 上的 RenderQueue 各自填好，再做一次
            跨 pass 的 PipelineKey 全局去重。

            可以先带着一个问题阅读：为什么 core 层的 FrameGraph 不做 pass reorder、不做
            attachment 复用、不做依赖分析？答案是这些都属于 backend 渲染图的职责；
            core 层这层"frame graph"只承担"per-pass per-scene 预构建"的薄壳角色，
            把语义留给 RenderQueue 和 backend。
            """
        ).strip(),
        related_sources=(
            "src/core/frame_graph/frame_graph.cpp",
        ),
        nav_order=650,
    ),
    SourceAnalysisTarget(
        source="src/core/asset/texture.hpp",
        output="notes/source_analysis/src/core/asset/texture.md",
        title="Texture 与 CombinedTextureSampler：CPU 图像如何进入 GPU 资源路径",
        intro=textwrap.dedent(
            """\
            这一页从
            [src/core/asset/texture.hpp](../../../../../src/core/asset/texture.hpp)
            出发，关注的问题不是“怎么加载一张图”，而是：为什么项目要把纹理拆成
            `Texture`（CPU 数据）和 `CombinedTextureSampler`（`IGpuResource` 适配层）
            两个类型，以及这种切分如何让同一份像素数据在多个材质里安全复用。

            可以先带着一个问题阅读：`Texture` 明明已经持有图像字节，为什么不让它
            直接实现 `IGpuResource`？答案是，纯 CPU 图像属于资源加载侧，而 binding
            name 和 dirty 追踪属于材质实例侧，两者生命周期不同，合并会让共享变难。
            """
        ).strip(),
        nav_order=400,
    ),
]


def normalize_comment_body(body: str) -> str:
    lines = textwrap.dedent(body).splitlines()
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines).strip()


def extract_sections(source_text: str) -> list[tuple[str, str]]:
    sections: list[tuple[str, str]] = []
    for match in SECTION_RE.finditer(source_text):
        title = match.group("title").strip()
        body = normalize_comment_body(match.group("body"))
        sections.append((title, body))
    return sections


def load_extra_section(output_path: Path) -> str:
    if not output_path.is_file():
        return ""
    text = output_path.read_text(encoding="utf-8")
    if EXTRA_MARKER not in text:
        return ""
    return text.split(EXTRA_MARKER, 1)[1].lstrip("\n")


def format_source_link(source: str) -> str:
    source_path = Path(source)
    depth = len(Path(source).parts) - 1
    rel_prefix = "../" * (depth + 1)
    return f"[{source_path.name}]({rel_prefix}{source})"


def load_targets() -> list[SourceAnalysisTarget]:
    return list(TARGETS)


def render_target(target: SourceAnalysisTarget) -> str:
    primary_source_path = REPO_ROOT / target.source
    output_path = REPO_ROOT / target.output
    extra = load_extra_section(output_path).rstrip()
    all_sources = target.all_sources

    lines = [
        f"# {target.title}",
        "",
        "本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的",
        "`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。",
        "",
        target.intro,
        "",
        f"源码入口：{format_source_link(target.source)}",
        "",
    ]

    if len(all_sources) > 1:
        lines.append("关联源码：")
        lines.append("")
        for source in target.related_sources:
            lines.append(f"- {format_source_link(source)}")
        lines.append("")

    for source in all_sources:
        source_path = REPO_ROOT / source
        sections = extract_sections(source_path.read_text(encoding="utf-8"))
        if not sections:
            continue
        if len(all_sources) > 1:
            lines.append(f"## {Path(source).name}")
            lines.append("")
            lines.append(f"源码位置：{format_source_link(source)}")
            lines.append("")

        for section_title, body in sections:
            heading = "###" if len(all_sources) > 1 else "##"
            lines.append(f"{heading} {section_title}")
            lines.append("")
            lines.append(body)
            lines.append("")

    lines.extend(
        [
            EXTRA_MARKER,
            "",
            extra
            if extra
            else "## 补充说明\n\n这里可以继续补充源码之外的上下文；"
            "脚本重跑时会保留这一节。",
            "",
        ]
    )

    return "\n".join(lines)


def main() -> None:
    for target in load_targets():
        output_path = REPO_ROOT / target.output
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(render_target(target), encoding="utf-8")
        print(f"[source-analysis] wrote {target.output}")


if __name__ == "__main__":
    main()

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


REPO_ROOT = Path(__file__).resolve().parent.parent
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
            以及为什么 `CameraData`、`SkeletonData`、`MaterialParameterData`、
            `CombinedTextureSampler`、`VertexBuffer` / `IndexBuffer`
            这些看起来语义不同的类型会在这里汇合。

            可以先带着一个问题阅读：为什么项目没有把“CPU 侧对象”和
            “backend 侧 upload/bind 输入”拆成更多层？答案是，这里故意只保留
            backend 真正需要的最小公共契约，避免每条资源路径都发明一套专用接口。
            """
        ).strip(),
    ),
    SourceAnalysisTarget(
        source="src/core/asset/material_instance.hpp",
        output="notes/source_analysis/src/core/asset/material_instance.md",
        title="MaterialInstance：从模板到运行时账本",
        intro=textwrap.dedent(
            """\
            这一页不是重新抄一遍 API，而是顺着
            [src/core/asset/material_instance.hpp](../../../../../src/core/asset/material_instance.hpp)
            的类型定义来看：这个类为什么要拆成 `MaterialParameterData`
            和 `MaterialInstance` 两层，以及为什么它现在坚持
            “单一 canonical 参数集 + pass 级只读筛选”这条主线。

            可以先带着一个问题阅读：`MaterialTemplate` 已经定义了 pass 和 shader，
            为什么还需要一个 `MaterialInstance`？答案是，template 负责“结构是什么”，
            instance 负责“这一帧真正要喂给 backend 的实例数据是什么”。
            """
        ).strip(),
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


def render_target(target: SourceAnalysisTarget) -> str:
    source_path = REPO_ROOT / target.source
    output_path = REPO_ROOT / target.output
    sections = extract_sections(source_path.read_text(encoding="utf-8"))
    extra = load_extra_section(output_path).rstrip()

    lines = [
        f"# {target.title}",
        "",
        "本页的主体内容由 `scripts/extract_source_analysis.py` 从源码中的",
        "`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。",
        "",
        target.intro,
        "",
        f"源码入口：[{source_path.name}](../../../../../{target.source})",
        "",
    ]

    for section_title, body in sections:
        lines.append(f"## {section_title}")
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
    for target in TARGETS:
        output_path = REPO_ROOT / target.output
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(render_target(target), encoding="utf-8")
        print(f"[source-analysis] wrote {target.output}")


if __name__ == "__main__":
    main()

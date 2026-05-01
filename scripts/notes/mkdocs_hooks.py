"""mkdocs hooks — 由 mkdocs.gen.yml 引用。

两件事:
    1. 历史上 requirements/*.md 可能从 notes/ 外部目录挂进站点，
       文档里会留下 ../../notes/xxx.md 这类相对链接。
       现在 requirements 已并入 notes/requirements/，这条重写仅作为兼容路径保留。
       `on_page_markdown` 仅对 requirements/ 下的页面重写 ../../notes/ → ../。

    2. 默认 toc 扩展的 slugify 会剥掉全部非 ASCII 字符，导致含中文的标题
       （例如 `P-1 确定性是架构级不变量`）只能生成 `p-1` 这样的锚点，
       phase-*.md 中对 principles.md#p-1-确定性是架构级不变量 这类链接全部失效。
       `on_config` 把 toc.slugify 替换为 pymdownx.slugs.slugify(case='lower')，
       让它保留 CJK，同时与 phase-*.md 里手写的锚点对齐。
"""

from __future__ import annotations

import re

from pymdownx.slugs import slugify as _pymdownx_slugify

_LINK_RE = re.compile(r"(\[[^\]]*\]\()([^)\s]+)((?:\s+\"[^\"]*\")?\))")
_PREFIX_RE = re.compile(r"^\.\./\.\./notes/")


def on_config(config):
    mdx = config.setdefault("mdx_configs", {})
    toc_cfg = mdx.setdefault("toc", {})
    toc_cfg["slugify"] = _pymdownx_slugify(case="lower")
    return config


def on_page_markdown(markdown: str, *, page, config, files) -> str:
    src_path = page.file.src_path.replace("\\", "/")
    if not src_path.startswith("requirements/"):
        return markdown

    def rewrite(match: re.Match[str]) -> str:
        head, url, tail = match.group(1), match.group(2), match.group(3)
        if url.startswith(("http://", "https://", "mailto:", "#")):
            return match.group(0)
        new_url = _PREFIX_RE.sub("../", url)
        return f"{head}{new_url}{tail}"

    return _LINK_RE.sub(rewrite, markdown)

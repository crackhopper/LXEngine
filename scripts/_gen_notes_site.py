#!/usr/bin/env python3
# _gen_notes_site.py — 为 mkdocs 预览生成导航与动态链接页
#
# 行为:
#   1. 扫描 docs/requirements/*.md (不含 finished/ 子目录)
#   2. 在 notes/requirements/ 下建立对每个文件的符号链接 (清理过期链接)
#   3. 扫描 notes/tools/*.md 并生成 tools/index.md
#   4. 读取 notes/nav.yml 作为站点导航唯一来源
#   5. 读取 mkdocs.yml -> 注入 nav / watch / hooks -> 写出 mkdocs.gen.yml
#
# 由 scripts/serve-notes.sh 调用。

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent
REQ_SRC_DIR = REPO_ROOT / "docs" / "requirements"
NOTES_DIR = REPO_ROOT / "notes"
REQ_LINK_DIR = NOTES_DIR / "requirements"
TOOLS_DIR = NOTES_DIR / "tools"
ROADMAPS_DIR = NOTES_DIR / "roadmaps"
MKDOCS_SRC = REPO_ROOT / "mkdocs.yml"
MKDOCS_GEN = REPO_ROOT / "mkdocs.gen.yml"
NAV_CONFIG = NOTES_DIR / "nav.yml"

NAV_SECTION_TITLE = "需求（进行中）"
TOOLS_SECTION_TITLE = "相关工具"
HEADING_RE = re.compile(r"^#\s+(.+?)\s*$")


def discover_requirements() -> list[Path]:
    if not REQ_SRC_DIR.is_dir():
        return []
    files = [p for p in REQ_SRC_DIR.iterdir() if p.is_file() and p.suffix == ".md"]
    files.sort(key=lambda p: p.name)
    return files


def discover_tools() -> list[Path]:
    if not TOOLS_DIR.is_dir():
        return []
    files = [
        p for p in TOOLS_DIR.iterdir()
        if p.is_file() and p.suffix == ".md" and p.name != "index.md"
    ]
    files.sort(key=lambda p: p.name)
    return files


def discover_roadmaps() -> list[Path]:
    if not ROADMAPS_DIR.is_dir():
        return []
    files = [
        p for p in ROADMAPS_DIR.iterdir()
        if p.is_file() and p.suffix == ".md" and p.name != "README.md"
    ]
    files.sort(key=lambda p: p.name)
    return files


def extract_title(md_path: Path, fallback: str) -> str:
    try:
        with md_path.open("r", encoding="utf-8") as f:
            for line in f:
                m = HEADING_RE.match(line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    return fallback


def sync_symlinks(req_files: list[Path]) -> None:
    REQ_LINK_DIR.mkdir(parents=True, exist_ok=True)

    wanted = {p.name for p in req_files}

    for entry in REQ_LINK_DIR.iterdir():
        if entry.is_symlink() or entry.is_file():
            if entry.name not in wanted or entry.name == "index.md":
                if entry.name != "index.md":
                    entry.unlink()

    for src in req_files:
        link = REQ_LINK_DIR / src.name
        target = os.path.relpath(src, REQ_LINK_DIR)
        if link.is_symlink() or link.exists():
            try:
                if link.is_symlink() and os.readlink(link) == target:
                    continue
            except OSError:
                pass
            link.unlink()
        link.symlink_to(target)

    write_index(req_files)


def write_index(req_files: list[Path]) -> None:
    index_path = REQ_LINK_DIR / "index.md"
    lines = [
        "# 需求（进行中）",
        "",
        "本目录由 `scripts/_gen_notes_site.py` 自动生成，列出 `docs/requirements/` 下尚未归档的需求文档。",
        "",
    ]
    for p in req_files:
        title = extract_title(p, p.stem)
        lines.append(f"- [{title}]({p.name})")
    lines.append("")
    index_path.write_text("\n".join(lines), encoding="utf-8")


def write_tools_index(tool_files: list[Path]) -> None:
    TOOLS_DIR.mkdir(parents=True, exist_ok=True)
    index_path = TOOLS_DIR / "index.md"
    lines = [
        "# 相关工具",
        "",
        "本目录收录 `notes/tools/` 下的工具说明文档。",
        "",
    ]
    for p in tool_files:
        title = extract_title(p, p.stem)
        lines.append(f"- [{title}]({p.name})")
    lines.append("")
    index_path.write_text("\n".join(lines), encoding="utf-8")


def validate_note_path(path_str: str, context: str) -> str:
    note_path = NOTES_DIR / path_str
    if not note_path.is_file():
        raise ValueError(f"{context}: missing notes file '{path_str}'")
    return path_str


def rel_note_path(path: Path) -> str:
    return path.relative_to(NOTES_DIR).as_posix()


def build_generated_nav_item(md_path: Path, nav_path: str | None = None) -> dict:
    path_str = nav_path if nav_path is not None else rel_note_path(md_path)
    return {extract_title(md_path, md_path.stem): path_str}


def expand_nav_token(token: str, req_files: list[Path], roadmap_files: list[Path]) -> list[dict]:
    if token == "@requirements":
        return [
            build_generated_nav_item(p, f"requirements/{p.name}")
            for p in req_files
        ]
    if token == "@roadmaps":
        return [build_generated_nav_item(p) for p in roadmap_files]
    raise ValueError(f"unsupported nav token '{token}'")


def normalize_nav_list(entries: list[object], context: str, req_files: list[Path], roadmap_files: list[Path]) -> list:
    normalized: list = []
    for index, entry in enumerate(entries):
        entry_context = f"{context}[{index}]"
        if isinstance(entry, str) and entry.startswith("@"):
            normalized.extend(expand_nav_token(entry, req_files, roadmap_files))
            continue
        normalized.append(normalize_nav_entry(entry, entry_context, req_files, roadmap_files))
    return normalized


def normalize_nav_entry(
    entry: object,
    context: str,
    req_files: list[Path],
    roadmap_files: list[Path],
) -> object:
    if isinstance(entry, str):
        return validate_note_path(entry, context)

    if isinstance(entry, dict):
        if len(entry) != 1:
            raise ValueError(f"{context}: nav mapping must contain exactly one title")

        title, value = next(iter(entry.items()))
        if not isinstance(title, str) or not title.strip():
            raise ValueError(f"{context}: nav title must be a non-empty string")

        child_context = f"{context} -> {title}"
        if isinstance(value, str):
            return {title: validate_note_path(value, child_context)}
        if isinstance(value, list):
            return {title: normalize_nav_list(value, child_context, req_files, roadmap_files)}

        raise ValueError(f"{child_context}: nav value must be a path or list")

    raise ValueError(f"{context}: unsupported nav entry type {type(entry).__name__}")


def load_nav_config(req_files: list[Path], roadmap_files: list[Path]) -> list:
    if not NAV_CONFIG.is_file():
        raise FileNotFoundError(f"{NAV_CONFIG} not found")

    with NAV_CONFIG.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}

    nav = cfg.get("nav")
    if not isinstance(nav, list) or not nav:
        raise ValueError("notes/nav.yml must define a non-empty 'nav' list")

    return normalize_nav_list(nav, "nav", req_files, roadmap_files)


def inject_into_mkdocs(req_files: list[Path], tool_files: list[Path], nav: list) -> None:
    with MKDOCS_SRC.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    cfg["nav"] = nav

    watch = cfg.get("watch") or []
    rel_req = os.path.relpath(REQ_SRC_DIR, REPO_ROOT)
    if rel_req not in watch:
        watch.append(rel_req)
    rel_notes = os.path.relpath(NOTES_DIR, REPO_ROOT)
    if rel_notes not in watch:
        watch.append(rel_notes)
    rel_nav = os.path.relpath(NAV_CONFIG, REPO_ROOT)
    if rel_nav not in watch:
        watch.append(rel_nav)
    mkdocs_src = os.path.relpath(MKDOCS_SRC, REPO_ROOT)
    if mkdocs_src not in watch:
        watch.append(mkdocs_src)
    cfg["watch"] = watch

    hooks = cfg.get("hooks") or []
    hook_path = "scripts/_notes_hooks.py"
    if hook_path not in hooks:
        hooks.append(hook_path)
    cfg["hooks"] = hooks

    header = (
        "# AUTO-GENERATED by scripts/_gen_notes_site.py — DO NOT EDIT.\n"
        "# Source: mkdocs.yml + notes/nav.yml + docs/requirements/*.md + notes/tools/*.md\n"
    )
    with MKDOCS_GEN.open("w", encoding="utf-8") as f:
        f.write(header)
        yaml.safe_dump(cfg, f, allow_unicode=True, sort_keys=False)


def main() -> int:
    if not MKDOCS_SRC.is_file():
        print(f"Error: {MKDOCS_SRC} not found", file=sys.stderr)
        return 1

    req_files = discover_requirements()
    tool_files = discover_tools()
    roadmap_files = discover_roadmaps()
    sync_symlinks(req_files)
    write_tools_index(tool_files)
    nav = load_nav_config(req_files, roadmap_files)
    inject_into_mkdocs(req_files, tool_files, nav)

    print(f">> Generated {MKDOCS_GEN.relative_to(REPO_ROOT)}")
    print(f"   nav entries: {len(nav)}")
    print(f"   {TOOLS_SECTION_TITLE}: {len(tool_files)} 篇")
    for p in tool_files:
        print(f"     - {p.name}")
    print(f"   {NAV_SECTION_TITLE}: {len(req_files)} 篇")
    for p in req_files:
        print(f"     - {p.name}")
    print(f"   Roadmap: {len(roadmap_files)} 篇")
    for p in roadmap_files:
        print(f"     - {p.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# _gen_notes_site.py — 为 mkdocs 预览生成动态导航
#
# 行为:
#   1. 扫描 docs/requirements/*.md (不含 finished/ 子目录)
#   2. 在 notes/requirements/ 下建立对每个文件的符号链接 (清理过期链接)
#   3. 扫描 notes/tools/*.md 并生成 tools/index.md
#   4. 扫描 notes/ 目录树，按目录结构生成层级导航
#   5. 读取 mkdocs.yml -> 注入动态 nav 段 + watch 段 -> 写出 mkdocs.gen.yml
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
MKDOCS_SRC = REPO_ROOT / "mkdocs.yml"
MKDOCS_GEN = REPO_ROOT / "mkdocs.gen.yml"

NAV_SECTION_TITLE = "需求 (进行中)"
TOOLS_SECTION_TITLE = "Tools"
HEADING_RE = re.compile(r"^#\s+(.+?)\s*$")
SKIP_NOTE_FILES = {"0324-after-implement.md"}
DIR_TITLES = {
    "tools": "Tools",
    "subsystems": "子系统",
    "tutorial": "Tutorial",
    "roadmaps": "Roadmap",
    "requirements": NAV_SECTION_TITLE,
}


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


def prettify_stem(name: str) -> str:
    parts = [part for part in re.split(r"[-_]+", name) if part]
    if not parts:
        return name
    return " ".join(part.capitalize() for part in parts)


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
        "# 需求 (进行中)",
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
        "# Tools",
        "",
        "本目录收录 `notes/tools/` 下的工具说明文档。",
        "",
    ]
    for p in tool_files:
        title = extract_title(p, p.stem)
        lines.append(f"- [{title}]({p.name})")
    lines.append("")
    index_path.write_text("\n".join(lines), encoding="utf-8")


def rel_note_path(path: Path) -> str:
    return path.relative_to(NOTES_DIR).as_posix()


def build_md_nav_item(md_path: Path) -> dict:
    return {extract_title(md_path, prettify_stem(md_path.stem)): rel_note_path(md_path)}


def discover_dir_index(dir_path: Path) -> Path | None:
    for name in ("index.md", "README.md"):
        candidate = dir_path / name
        if candidate.is_file():
            return candidate
    return None


def dir_display_title(dir_path: Path, index_path: Path | None) -> str:
    override = DIR_TITLES.get(dir_path.name)
    if override:
        return override
    if index_path is not None:
        return extract_title(index_path, prettify_stem(dir_path.name))
    return prettify_stem(dir_path.name)


def build_dir_nav_section(dir_path: Path) -> dict | None:
    entries: list = []
    index_path = discover_dir_index(dir_path)
    if index_path is not None:
        entries.append(rel_note_path(index_path))

    files = [
        p for p in dir_path.iterdir()
        if p.is_file()
        and p.suffix == ".md"
        and p.name not in {"index.md", "README.md"}
        and not p.name.startswith(".")
        and p.name not in SKIP_NOTE_FILES
    ]
    files.sort(key=lambda p: p.name)
    for md_path in files:
        entries.append(build_md_nav_item(md_path))

    subdirs = [p for p in dir_path.iterdir() if p.is_dir() and not p.name.startswith(".")]
    subdirs.sort(key=lambda p: p.name)
    for child in subdirs:
        section = build_dir_nav_section(child)
        if section:
            entries.append(section)

    if not entries:
        return None

    return {dir_display_title(dir_path, index_path): entries}


def build_notes_nav() -> list:
    nav: list = []
    root_files = [
        p for p in NOTES_DIR.iterdir()
        if p.is_file()
        and p.suffix == ".md"
        and not p.name.startswith(".")
        and p.name not in SKIP_NOTE_FILES
    ]
    root_files.sort(key=lambda p: p.name)
    for md_path in root_files:
        nav.append(build_md_nav_item(md_path))

    root_dirs = [
        p for p in NOTES_DIR.iterdir()
        if p.is_dir() and not p.name.startswith(".")
    ]
    root_dirs.sort(key=lambda p: p.name)
    for dir_path in root_dirs:
        section = build_dir_nav_section(dir_path)
        if section:
            nav.append(section)

    return nav


def inject_into_mkdocs(req_files: list[Path], tool_files: list[Path]) -> None:
    with MKDOCS_SRC.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    nav = build_notes_nav()
    cfg["nav"] = nav

    watch = cfg.get("watch") or []
    rel_req = os.path.relpath(REQ_SRC_DIR, REPO_ROOT)
    if rel_req not in watch:
        watch.append(rel_req)
    rel_notes = os.path.relpath(NOTES_DIR, REPO_ROOT)
    if rel_notes not in watch:
        watch.append(rel_notes)
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
        "# Source: mkdocs.yml + docs/requirements/*.md + notes/tools/*.md\n"
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
    sync_symlinks(req_files)
    write_tools_index(tool_files)
    inject_into_mkdocs(req_files, tool_files)

    print(f">> Generated {MKDOCS_GEN.relative_to(REPO_ROOT)}")
    print(f"   notes nav sections: {len(build_notes_nav())}")
    print(f"   {TOOLS_SECTION_TITLE}: {len(tool_files)} 篇")
    for p in tool_files:
        print(f"     - {p.name}")
    print(f"   {NAV_SECTION_TITLE}: {len(req_files)} 篇")
    for p in req_files:
        print(f"     - {p.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

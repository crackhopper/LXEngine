#!/usr/bin/env python3
# generate_site_config.py — 为 mkdocs 预览生成导航与动态索引页
#
# 行为:
#   1. 扫描 notes/requirements/*.md (不含 finished/ 子目录)
#   2. 确保 notes/requirements/index.md 存在 (否则生成一份默认索引)
#   3. 扫描 notes/tools/*.md 并生成 tools/index.md
#   4. 读取 notes/nav.yml 作为站点导航唯一来源
#   5. 读取 mkdocs.yml -> 注入 nav / watch / hooks -> 写出 mkdocs.gen.yml
#
# 由 scripts/notes/serve_site.sh 调用。

from __future__ import annotations

import os
import re
import sys
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
NOTES_DIR = REPO_ROOT / "notes"
REQ_DIR = NOTES_DIR / "requirements"
TOOLS_DIR = NOTES_DIR / "tools"
ROADMAPS_DIR = NOTES_DIR / "roadmaps"
TEMPORARY_DIR = NOTES_DIR / "temporary"
MKDOCS_SRC = REPO_ROOT / "mkdocs.yml"
MKDOCS_GEN = REPO_ROOT / "mkdocs.gen.yml"
NAV_CONFIG = NOTES_DIR / "nav.yml"
SOURCE_ANALYSIS_SCRIPT = (
    REPO_ROOT / "scripts" / "source_analysis" / "extract_sections.py"
)
HOT_RELOAD_JS = "assets/javascripts/notes-hot-reload.js"
CHAT_JS = "assets/javascripts/notes-chat.js"

NAV_SECTION_TITLE = "需求（进行中）"
TOOLS_SECTION_TITLE = "相关工具"
TEMPORARY_SECTION_TITLE = "临时笔记"
HEADING_RE = re.compile(r"^#\s+(.+?)\s*$")
NATURAL_TOKEN_RE = re.compile(r"(\d+)")


def natural_name_key(name: str) -> list[tuple[int, object]]:
    parts = NATURAL_TOKEN_RE.split(name.lower())
    key: list[tuple[int, object]] = []
    for part in parts:
        if not part:
            continue
        if part.isdigit():
            key.append((0, int(part)))
        else:
            key.append((1, part))
    return key


def discover_requirements() -> list[Path]:
    if not REQ_DIR.is_dir():
        return []
    files = [
        p for p in REQ_DIR.iterdir()
        if p.is_file() and p.suffix == ".md" and p.name != "index.md"
    ]
    files.sort(key=lambda p: natural_name_key(p.name))
    return files


def discover_tools() -> list[Path]:
    if not TOOLS_DIR.is_dir():
        return []
    files = [
        p for p in TOOLS_DIR.iterdir()
        if p.is_file() and p.suffix == ".md" and p.name != "index.md"
    ]
    files.sort(key=lambda p: natural_name_key(p.name))
    return files


def discover_roadmap_dirs(parent: Path) -> list[Path]:
    if not parent.is_dir():
        return []
    dirs = [p for p in parent.iterdir() if p.is_dir()]
    dirs.sort(key=lambda p: natural_name_key(p.name))
    return dirs


def discover_roadmap_files(parent: Path) -> list[Path]:
    if not parent.is_dir():
        return []
    files = [
        p for p in parent.iterdir()
        if p.is_file() and p.suffix == ".md" and p.name != "README.md"
    ]
    files.sort(key=lambda p: natural_name_key(p.name))
    return files


def discover_roadmaps() -> list[Path]:
    return discover_roadmap_dirs(ROADMAPS_DIR)


def discover_temporary() -> list[Path]:
    """Pick up anything under notes/temporary/ as stand-alone short-lived notes.

    Intended for resize-fix writeups, incident post-mortems, day-of-debugging
    notes — anything that doesn't deserve a permanent slot in the design docs
    but is useful to index. Sort by filename so date-prefixed names surface
    chronologically.
    """
    if not TEMPORARY_DIR.is_dir():
        return []
    files = [
        p for p in TEMPORARY_DIR.iterdir()
        if p.is_file() and p.suffix == ".md" and p.name != "index.md"
    ]
    files.sort(key=lambda p: natural_name_key(p.name))
    return files


def load_source_analysis_targets() -> list[object]:
    if not SOURCE_ANALYSIS_SCRIPT.is_file():
        return []

    spec = spec_from_file_location("extract_source_analysis", SOURCE_ANALYSIS_SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load {SOURCE_ANALYSIS_SCRIPT}")

    module = module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    if not hasattr(module, "load_targets"):
        raise RuntimeError("extract_sections.py missing load_targets()")

    targets = list(module.load_targets())
    targets.sort(key=lambda target: (target.nav_order, target.title))
    return targets


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


def write_text_if_changed(path: Path, content: str) -> bool:
    try:
        if path.is_file() and path.read_text(encoding="utf-8") == content:
            return False
    except OSError:
        pass
    path.write_text(content, encoding="utf-8")
    return True


def truthy(value: object) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.lower() in {"1", "true", "yes", "on"}
    return False


def chat_enabled(cfg: dict) -> bool:
    env_value = os.environ.get("NOTES_CHAT_ENABLED")
    if env_value is not None:
        return truthy(env_value)

    extra = cfg.get("extra") or {}
    notes_chat = extra.get("notes_chat") or {}
    return truthy(notes_chat.get("enabled"))


def write_index(req_files: list[Path]) -> None:
    REQ_DIR.mkdir(parents=True, exist_ok=True)
    index_path = REQ_DIR / "index.md"

    if index_path.is_file():
        return

    lines = [
        "# 需求（进行中）",
        "",
        "本目录由 `scripts/notes/generate_site_config.py` 自动生成，列出 `notes/requirements/` 下尚未归档的需求文档。",
        "",
    ]
    for p in req_files:
        title = extract_title(p, p.stem)
        lines.append(f"- [{title}]({p.name})")
    lines.append("")
    write_text_if_changed(index_path, "\n".join(lines))


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
    write_text_if_changed(index_path, "\n".join(lines))


def write_temporary_index(temporary_files: list[Path]) -> None:
    TEMPORARY_DIR.mkdir(parents=True, exist_ok=True)
    index_path = TEMPORARY_DIR / "index.md"
    lines = [
        "# 临时笔记",
        "",
        "本目录收录 `notes/temporary/` 下的即时笔记 —— 调试记录、事故复盘、"
        "日常排查。文件名以日期前缀排序。不进入正式设计文档体系，但被 "
        "`serve_site` 自动索引以便回查。",
        "",
    ]
    for p in temporary_files:
        title = extract_title(p, p.stem)
        lines.append(f"- [{title}]({p.name})")
    lines.append("")
    write_text_if_changed(index_path, "\n".join(lines))


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


def build_roadmap_dir_nav(dir_path: Path) -> dict:
    readme = dir_path / "README.md"
    title = extract_title(readme, dir_path.name) if readme.is_file() else dir_path.name

    children: list[object] = []
    if readme.is_file():
        children.append(rel_note_path(readme))

    for child_dir in discover_roadmap_dirs(dir_path):
        children.append(build_roadmap_dir_nav(child_dir))

    for md_path in discover_roadmap_files(dir_path):
        children.append(build_generated_nav_item(md_path))

    return {title: children}


def expand_nav_token(
    token: str,
    req_files: list[Path],
    roadmap_dirs: list[Path],
    temporary_files: list[Path],
    source_analysis_targets: list[object],
) -> list[dict]:
    if token == "@requirements":
        return [
            build_generated_nav_item(p, f"requirements/{p.name}")
            for p in req_files
        ]
    if token == "@roadmaps":
        return [build_roadmap_dir_nav(p) for p in roadmap_dirs]
    if token == "@temporary":
        return [build_generated_nav_item(p) for p in temporary_files]
    if token == "@source_analysis":
        return [{target.title: target.output.removeprefix("notes/")} for target in source_analysis_targets]
    raise ValueError(f"unsupported nav token '{token}'")


def normalize_nav_list(
    entries: list[object],
    context: str,
    req_files: list[Path],
    roadmap_dirs: list[Path],
    temporary_files: list[Path],
    source_analysis_targets: list[object],
) -> list:
    normalized: list = []
    for index, entry in enumerate(entries):
        entry_context = f"{context}[{index}]"
        if isinstance(entry, str) and entry.startswith("@"):
            normalized.extend(
                expand_nav_token(
                    entry,
                    req_files,
                    roadmap_dirs,
                    temporary_files,
                    source_analysis_targets,
                )
            )
            continue
        normalized.append(
            normalize_nav_entry(
                entry,
                entry_context,
                req_files,
                roadmap_dirs,
                temporary_files,
                source_analysis_targets,
            )
        )
    return normalized


def normalize_nav_entry(
    entry: object,
    context: str,
    req_files: list[Path],
    roadmap_dirs: list[Path],
    temporary_files: list[Path],
    source_analysis_targets: list[object],
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
            return {
                title: normalize_nav_list(
                    value,
                    child_context,
                    req_files,
                    roadmap_dirs,
                    temporary_files,
                    source_analysis_targets,
                )
            }

        raise ValueError(f"{child_context}: nav value must be a path or list")

    raise ValueError(f"{context}: unsupported nav entry type {type(entry).__name__}")


def load_nav_config(
    req_files: list[Path],
    roadmap_dirs: list[Path],
    temporary_files: list[Path],
    source_analysis_targets: list[object],
) -> list:
    if not NAV_CONFIG.is_file():
        raise FileNotFoundError(f"{NAV_CONFIG} not found")

    with NAV_CONFIG.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f) or {}

    nav = cfg.get("nav")
    if not isinstance(nav, list) or not nav:
        raise ValueError("notes/nav.yml must define a non-empty 'nav' list")

    return normalize_nav_list(
        nav,
        "nav",
        req_files,
        roadmap_dirs,
        temporary_files,
        source_analysis_targets,
    )


def inject_into_mkdocs(req_files: list[Path], tool_files: list[Path], nav: list) -> None:
    with MKDOCS_SRC.open("r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    cfg["nav"] = nav

    watch = cfg.get("watch") or []
    rel_req = os.path.relpath(REQ_DIR, REPO_ROOT)
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
    hook_path = "scripts/notes/mkdocs_hooks.py"
    if hook_path not in hooks:
        hooks.append(hook_path)
    cfg["hooks"] = hooks

    extra_javascript = cfg.get("extra_javascript") or []
    if HOT_RELOAD_JS not in extra_javascript:
        extra_javascript.append(HOT_RELOAD_JS)
    if chat_enabled(cfg) and CHAT_JS not in extra_javascript:
        extra_javascript.append(CHAT_JS)
    cfg["extra_javascript"] = extra_javascript

    header = (
        "# AUTO-GENERATED by scripts/notes/generate_site_config.py — DO NOT EDIT.\n"
        "# Source: mkdocs.yml + notes/nav.yml + notes/requirements/*.md + notes/tools/*.md\n"
    )
    rendered = header + yaml.safe_dump(cfg, allow_unicode=True, sort_keys=False)
    MKDOCS_GEN.write_text(rendered, encoding="utf-8")


def main() -> int:
    if not MKDOCS_SRC.is_file():
        print(f"Error: {MKDOCS_SRC} not found", file=sys.stderr)
        return 1

    req_files = discover_requirements()
    tool_files = discover_tools()
    roadmap_dirs = discover_roadmaps()
    temporary_files = discover_temporary()
    source_analysis_targets = load_source_analysis_targets()
    write_index(req_files)
    write_tools_index(tool_files)
    write_temporary_index(temporary_files)
    nav = load_nav_config(req_files, roadmap_dirs, temporary_files, source_analysis_targets)
    inject_into_mkdocs(req_files, tool_files, nav)

    print(f">> Generated {MKDOCS_GEN.relative_to(REPO_ROOT)}")
    print(f"   nav entries: {len(nav)}")
    print(f"   {TOOLS_SECTION_TITLE}: {len(tool_files)} 篇")
    for p in tool_files:
        print(f"     - {p.name}")
    print(f"   {NAV_SECTION_TITLE}: {len(req_files)} 篇")
    for p in req_files:
        print(f"     - {p.name}")
    print(f"   Roadmap 分组: {len(roadmap_dirs)} 组")
    for p in roadmap_dirs:
        print(f"     - {p.relative_to(ROADMAPS_DIR).as_posix()}/")
    print(f"   {TEMPORARY_SECTION_TITLE}: {len(temporary_files)} 篇")
    for p in temporary_files:
        print(f"     - {p.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

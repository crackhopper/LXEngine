#!/usr/bin/env python3
# generate_site_config.py — 为 mkdocs 预览生成导航与动态索引页
#
# 行为:
#   1. 扫描 notes/requirements/*.md (不含 index.md / README.md / finished/)，按文件名编号展示实施队列
#   2. 确保 notes/requirements/index.md 存在 (否则生成一份默认索引)
#   3. 扫描 notes/tools/*.md 并生成 tools/index.md
#   4. 镜像近期活跃的 docs/superpowers/specs 与 plans 到 notes/superpowers/
#   5. 读取 notes/nav.yml 作为站点导航唯一来源
#   6. 读取 mkdocs.yml -> 注入 nav / watch / hooks -> 写出 mkdocs.gen.yml
#
# 由 scripts/notes/serve_site.sh 调用。

from __future__ import annotations

import json
import os
import re
import shutil
import sys
from dataclasses import dataclass
from datetime import date, timedelta
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
NOTES_DIR = REPO_ROOT / "notes"
REQ_DIR = NOTES_DIR / "requirements"
TOOLS_DIR = NOTES_DIR / "tools"
ROADMAPS_DIR = NOTES_DIR / "roadmaps"
TEMPORARY_DIR = NOTES_DIR / "temporary"
SUPERPOWERS_SRC_DIR = REPO_ROOT / "docs" / "superpowers"
SUPERPOWERS_SPECS_SRC_DIR = SUPERPOWERS_SRC_DIR / "specs"
SUPERPOWERS_PLANS_SRC_DIR = SUPERPOWERS_SRC_DIR / "plans"
SUPERPOWERS_NOTES_DIR = NOTES_DIR / "superpowers"
SUPERPOWERS_SPECS_NOTES_DIR = SUPERPOWERS_NOTES_DIR / "specs"
SUPERPOWERS_PLANS_NOTES_DIR = SUPERPOWERS_NOTES_DIR / "plans"
MKDOCS_SRC = REPO_ROOT / "mkdocs.yml"
MKDOCS_GEN = REPO_ROOT / "mkdocs.gen.yml"
NAV_CONFIG = NOTES_DIR / "nav.yml"
SOURCE_ANALYSIS_SCRIPT = (
    REPO_ROOT / "scripts" / "source_analysis" / "extract_sections.py"
)
HOT_RELOAD_JS = "assets/javascripts/notes-hot-reload.js"
CHAT_CONFIG_JS = "assets/javascripts/notes-chat-config.js"
CHAT_JS = "assets/javascripts/notes-chat.js"
WILDCARD_CHAT_HOSTS = {"0.0.0.0", "::", "[::]"}

NAV_SECTION_TITLE = "需求（进行中）"
TOOLS_SECTION_TITLE = "相关工具"
TEMPORARY_SECTION_TITLE = "临时笔记"
SUPERPOWERS_SECTION_TITLE = "Superpowers"
SUPERPOWERS_SPECS_SECTION_TITLE = "设计 Specs"
SUPERPOWERS_PLANS_SECTION_TITLE = "执行 Plans"
DEFAULT_SUPERPOWERS_RECENT_DAYS = 0
DEFAULT_SUPERPOWERS_ACTIVE_REQS = "073"
HEADING_RE = re.compile(r"^#\s+(.+?)\s*$")
NATURAL_TOKEN_RE = re.compile(r"(\d+)")
DATED_SUPERPOWERS_RE = re.compile(r"^(\d{4})-(\d{2})-(\d{2})-.+\.md$")
REQ_TOKEN_RE = re.compile(r"(?<!\d)(\d{3})(?:-?([a-z]))?(?!\d)")


@dataclass(frozen=True)
class SuperpowersDoc:
    source: Path
    target: Path
    nav_path: str
    title: str


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
        if (
            p.is_file()
            and p.suffix == ".md"
            and p.name not in {"index.md", "README.md"}
        )
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


def parse_dated_superpowers_name(name: str) -> date | None:
    m = DATED_SUPERPOWERS_RE.match(name)
    if not m:
        return None
    return date(int(m.group(1)), int(m.group(2)), int(m.group(3)))


def collect_active_requirement_tokens(req_files: list[Path]) -> set[str]:
    configured = os.environ.get(
        "NOTES_SUPERPOWERS_ACTIVE_REQS",
        DEFAULT_SUPERPOWERS_ACTIVE_REQS,
    )
    configured = configured.strip().lower()
    if configured and configured != "auto":
        return {
            token.strip()
            for token in re.split(r"[,;\s]+", configured)
            if token.strip()
        }

    tokens: set[str] = set()
    for req_file in req_files:
        m = REQ_TOKEN_RE.search(req_file.name.lower())
        if not m:
            continue
        number = m.group(1)
        suffix = m.group(2)
        tokens.add(number)
        if suffix:
            tokens.add(f"{number}-{suffix}")
            tokens.add(f"{number}{suffix}")
    return tokens


def is_active_superpowers_doc(path: Path, active_req_tokens: set[str]) -> bool:
    if path.name == "README.md":
        return False

    name = path.name.lower()
    if any(token in name for token in active_req_tokens):
        return True

    recent_days = int(
        os.environ.get("NOTES_SUPERPOWERS_RECENT_DAYS", str(DEFAULT_SUPERPOWERS_RECENT_DAYS))
    )
    if recent_days <= 0:
        return False

    doc_date = parse_dated_superpowers_name(path.name)
    if doc_date is None:
        return False
    active_from = date.today() - timedelta(days=recent_days)
    return doc_date >= active_from


def discover_active_superpowers_docs(
    source_dir: Path,
    target_dir: Path,
    nav_prefix: str,
    active_req_tokens: set[str],
) -> list[SuperpowersDoc]:
    if not source_dir.is_dir():
        return []

    docs: list[SuperpowersDoc] = []
    for source in sorted(source_dir.glob("*.md"), key=lambda p: natural_name_key(p.name)):
        if not is_active_superpowers_doc(source, active_req_tokens):
            continue
        target = target_dir / source.name
        docs.append(
            SuperpowersDoc(
                source=source,
                target=target,
                nav_path=f"{nav_prefix}/{source.name}",
                title=extract_title(source, source.stem),
            )
        )
    return docs


def clean_generated_superpowers_dir() -> None:
    if SUPERPOWERS_NOTES_DIR.exists():
        shutil.rmtree(SUPERPOWERS_NOTES_DIR)
    SUPERPOWERS_SPECS_NOTES_DIR.mkdir(parents=True, exist_ok=True)
    SUPERPOWERS_PLANS_NOTES_DIR.mkdir(parents=True, exist_ok=True)


def copy_superpowers_docs(docs: list[SuperpowersDoc]) -> None:
    for doc in docs:
        doc.target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(doc.source, doc.target)


def write_superpowers_collection_index(
    target_dir: Path,
    title: str,
    docs: list[SuperpowersDoc],
) -> None:
    target_dir.mkdir(parents=True, exist_ok=True)
    lines = [
        f"# {title}",
        "",
        "本页由 `scripts/notes/generate_site_config.py` 自动生成。这里只镜像当前活跃需求编号命中的 Superpowers 文档。默认活跃编号为 `073`；可用 `NOTES_SUPERPOWERS_ACTIVE_REQS` 调整，或用 `NOTES_SUPERPOWERS_RECENT_DAYS` 临时打开最近 N 天窗口。",
        "",
    ]
    if docs:
        for doc in docs:
            source_rel = doc.source.relative_to(REPO_ROOT).as_posix()
            lines.append(f"- [{doc.title}]({doc.target.name}) — `{source_rel}`")
    else:
        lines.append("当前没有活跃文档。")
    lines.append("")
    write_text_if_changed(target_dir / "index.md", "\n".join(lines))


def write_superpowers_index(
    spec_docs: list[SuperpowersDoc],
    plan_docs: list[SuperpowersDoc],
) -> None:
    SUPERPOWERS_NOTES_DIR.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Superpowers 当前工作",
        "",
        "这个入口把 `docs/superpowers/specs/` 和 `docs/superpowers/plans/` 中近期活跃的文档镜像到 notes 站点，便于在浏览器里审阅设计和执行计划。",
        "",
        f"- [设计 Specs](specs/index.md)：{len(spec_docs)} 篇",
        f"- [执行 Plans](plans/index.md)：{len(plan_docs)} 篇",
        "",
        "历史已完成文档仍保留在 `docs/superpowers/`，但默认不进入网页导航，避免审阅当前需求时被旧内容淹没。",
        "",
    ]
    write_text_if_changed(SUPERPOWERS_NOTES_DIR / "index.md", "\n".join(lines))


def mirror_active_superpowers_docs(
    req_files: list[Path],
) -> tuple[list[SuperpowersDoc], list[SuperpowersDoc]]:
    active_req_tokens = collect_active_requirement_tokens(req_files)
    spec_docs = discover_active_superpowers_docs(
        SUPERPOWERS_SPECS_SRC_DIR,
        SUPERPOWERS_SPECS_NOTES_DIR,
        "superpowers/specs",
        active_req_tokens,
    )
    plan_docs = discover_active_superpowers_docs(
        SUPERPOWERS_PLANS_SRC_DIR,
        SUPERPOWERS_PLANS_NOTES_DIR,
        "superpowers/plans",
        active_req_tokens,
    )

    clean_generated_superpowers_dir()
    copy_superpowers_docs(spec_docs)
    copy_superpowers_docs(plan_docs)
    write_superpowers_collection_index(
        SUPERPOWERS_SPECS_NOTES_DIR,
        SUPERPOWERS_SPECS_SECTION_TITLE,
        spec_docs,
    )
    write_superpowers_collection_index(
        SUPERPOWERS_PLANS_NOTES_DIR,
        SUPERPOWERS_PLANS_SECTION_TITLE,
        plan_docs,
    )
    write_superpowers_index(spec_docs, plan_docs)
    return spec_docs, plan_docs


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


def chat_client_config(cfg: dict) -> dict[str, object]:
    extra = cfg.get("extra") or {}
    notes_chat = extra.get("notes_chat") or {}
    config: dict[str, object] = {}

    host = os.environ.get("NOTES_CHAT_CLIENT_HOST")
    if host is None:
        raw_host = notes_chat.get("host")
        host = raw_host if isinstance(raw_host, str) else ""
    host = host.strip()
    if host and host not in WILDCARD_CHAT_HOSTS:
        config["host"] = host

    port = os.environ.get("NOTES_CHAT_CLIENT_PORT")
    if port is None:
        raw_port = notes_chat.get("port")
        if isinstance(raw_port, int):
            port = str(raw_port)
        elif isinstance(raw_port, str):
            port = raw_port
        else:
            port = ""
    port = port.strip()
    if port:
        try:
            parsed_port = int(port)
        except ValueError:
            parsed_port = 0
        if parsed_port > 0:
            config["port"] = parsed_port

    return config


def render_chat_config_js(config: dict[str, object]) -> str:
    payload = json.dumps(config, ensure_ascii=False, sort_keys=True)
    return (
        "// AUTO-GENERATED by scripts/notes/generate_site_config.py.\n"
        f"window.NOTES_CHAT_CONFIG = {payload};\n"
    )


def remove_chat_config_js() -> None:
    try:
        (NOTES_DIR / CHAT_CONFIG_JS).unlink()
    except FileNotFoundError:
        pass


def write_index(req_files: list[Path]) -> None:
    REQ_DIR.mkdir(parents=True, exist_ok=True)
    index_path = REQ_DIR / "index.md"

    lines = [
        "# 需求（进行中）",
        "",
        "本目录由 `scripts/notes/generate_site_config.py` 自动生成，列出 `notes/requirements/` 下尚未归档的需求文档；文件名编号即建议实施顺序，一个 REQ 文件只覆盖一个连续实施周期。",
        "",
    ]
    if req_files:
        for p in req_files:
            title = extract_title(p, p.stem)
            lines.append(f"- [{title}]({p.name})")
    else:
        lines.append("当前没有进行中的需求文档。")
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
    superpowers_spec_docs: list[SuperpowersDoc],
    superpowers_plan_docs: list[SuperpowersDoc],
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
    if token == "@superpowers_specs":
        return [
            build_generated_nav_item(doc.target, doc.nav_path)
            for doc in superpowers_spec_docs
        ]
    if token == "@superpowers_plans":
        return [
            build_generated_nav_item(doc.target, doc.nav_path)
            for doc in superpowers_plan_docs
        ]
    raise ValueError(f"unsupported nav token '{token}'")


def normalize_nav_list(
    entries: list[object],
    context: str,
    req_files: list[Path],
    roadmap_dirs: list[Path],
    temporary_files: list[Path],
    source_analysis_targets: list[object],
    superpowers_spec_docs: list[SuperpowersDoc],
    superpowers_plan_docs: list[SuperpowersDoc],
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
                    superpowers_spec_docs,
                    superpowers_plan_docs,
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
                superpowers_spec_docs,
                superpowers_plan_docs,
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
    superpowers_spec_docs: list[SuperpowersDoc],
    superpowers_plan_docs: list[SuperpowersDoc],
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
                    superpowers_spec_docs,
                    superpowers_plan_docs,
                )
            }

        raise ValueError(f"{child_context}: nav value must be a path or list")

    raise ValueError(f"{context}: unsupported nav entry type {type(entry).__name__}")


def load_nav_config(
    req_files: list[Path],
    roadmap_dirs: list[Path],
    temporary_files: list[Path],
    source_analysis_targets: list[object],
    superpowers_spec_docs: list[SuperpowersDoc],
    superpowers_plan_docs: list[SuperpowersDoc],
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
        superpowers_spec_docs,
        superpowers_plan_docs,
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
    rel_superpowers = os.path.relpath(SUPERPOWERS_SRC_DIR, REPO_ROOT)
    if rel_superpowers not in watch:
        watch.append(rel_superpowers)
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
    if chat_enabled(cfg):
        if CHAT_CONFIG_JS not in extra_javascript:
            extra_javascript.append(CHAT_CONFIG_JS)
        if CHAT_JS not in extra_javascript:
            extra_javascript.append(CHAT_JS)
        chat_config_path = NOTES_DIR / CHAT_CONFIG_JS
        chat_config_path.parent.mkdir(parents=True, exist_ok=True)
        write_text_if_changed(chat_config_path, render_chat_config_js(chat_client_config(cfg)))
    else:
        remove_chat_config_js()
    cfg["extra_javascript"] = extra_javascript

    header = (
        "# AUTO-GENERATED by scripts/notes/generate_site_config.py — DO NOT EDIT.\n"
        "# Source: mkdocs.yml + notes/nav.yml + notes/requirements/*.md + notes/tools/*.md + docs/superpowers/{specs,plans}/*.md\n"
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
    superpowers_spec_docs, superpowers_plan_docs = mirror_active_superpowers_docs(req_files)
    nav = load_nav_config(
        req_files,
        roadmap_dirs,
        temporary_files,
        source_analysis_targets,
        superpowers_spec_docs,
        superpowers_plan_docs,
    )
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
    print(f"   {SUPERPOWERS_SECTION_TITLE}/{SUPERPOWERS_SPECS_SECTION_TITLE}: {len(superpowers_spec_docs)} 篇")
    for doc in superpowers_spec_docs:
        print(f"     - {doc.source.relative_to(SUPERPOWERS_SRC_DIR).as_posix()}")
    print(f"   {SUPERPOWERS_SECTION_TITLE}/{SUPERPOWERS_PLANS_SECTION_TITLE}: {len(superpowers_plan_docs)} 篇")
    for doc in superpowers_plan_docs:
        print(f"     - {doc.source.relative_to(SUPERPOWERS_SRC_DIR).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

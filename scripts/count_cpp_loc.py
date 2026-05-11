#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


DEFAULT_EXCLUDED_DIRS = [
    "third_party",
    "src/infra/external",
    "build",
    ".git",
    ".site",
    ".tmp",
    "tmp",
]
CPP_EXTENSIONS = {".cpp", ".cc", ".cxx"}
HEADER_EXTENSIONS = {".hpp", ".hh", ".hxx", ".h"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Count project-owned C++ lines while excluding bundled third-party code.",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Repository root to scan. Defaults to this script's repository root.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON instead of text output.",
    )
    parser.add_argument(
        "--by-dir",
        nargs="+",
        metavar="DIR",
        help="Also report per-directory totals for the given relative directories.",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="DIR",
        help="Add another relative directory to exclude.",
    )
    parser.add_argument(
        "--code-only",
        action="store_true",
        help="Exclude blank lines and pure comment lines.",
    )
    return parser.parse_args()


def normalize_relpath(path: Path) -> str:
    return path.as_posix().strip("/")


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def count_all_lines(path: Path) -> int:
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        return sum(1 for _ in handle)


def count_code_only_lines(path: Path) -> int:
    count = 0
    in_block_comment = False

    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for raw_line in handle:
            line = raw_line.rstrip("\n")
            index = 0
            has_code = False

            while index < len(line):
                if in_block_comment:
                    end = line.find("*/", index)
                    if end == -1:
                        index = len(line)
                        break
                    in_block_comment = False
                    index = end + 2
                    continue

                if line.startswith("//", index):
                    break

                if line.startswith("/*", index):
                    in_block_comment = True
                    index += 2
                    continue

                if line[index].isspace():
                    index += 1
                    continue

                has_code = True
                break

            if has_code:
                count += 1

    return count


def build_file_records(
    root: Path,
    excluded_dirs: list[str],
    code_only: bool,
) -> list[dict[str, object]]:
    excluded_paths = [root / rel for rel in excluded_dirs]
    records: list[dict[str, object]] = []

    for path in root.rglob("*"):
        if not path.is_file():
            continue

        suffix = path.suffix.lower()
        if suffix not in CPP_EXTENSIONS and suffix not in HEADER_EXTENSIONS:
            continue

        if any(is_relative_to(path, excluded_path) for excluded_path in excluded_paths):
            continue

        relpath = path.relative_to(root)
        records.append(
            {
                "path": relpath,
                "suffix": suffix,
                "lines": count_code_only_lines(path) if code_only else count_all_lines(path),
            }
        )

    return records


def summarize(records: list[dict[str, object]], allowed_extensions: set[str]) -> dict[str, int]:
    filtered = [record for record in records if record["suffix"] in allowed_extensions]
    return {
        "files": len(filtered),
        "lines": sum(int(record["lines"]) for record in filtered),
    }


def summarize_by_dir(records: list[dict[str, object]], dir_args: list[str]) -> dict[str, dict[str, int]]:
    totals: dict[str, dict[str, int]] = {}

    for dir_arg in dir_args:
        rel = normalize_relpath(Path(dir_arg))
        base = Path(rel)
        lines = 0
        files = 0

        for record in records:
            record_path = record["path"]
            assert isinstance(record_path, Path)
            if is_relative_to(record_path, base):
                files += 1
                lines += int(record["lines"])

        totals[rel] = {"files": files, "lines": lines}

    return totals


def build_report(args: argparse.Namespace) -> dict[str, object]:
    root = args.root.resolve()
    excluded_dirs = [*DEFAULT_EXCLUDED_DIRS, *args.exclude]
    records = build_file_records(root, excluded_dirs, args.code_only)

    report: dict[str, object] = {
        "root": str(root),
        "excluded_dirs": excluded_dirs,
        "mode": "code_only" if args.code_only else "all_lines",
        "cpp": summarize(records, CPP_EXTENSIONS),
        "cpp_and_headers": summarize(records, CPP_EXTENSIONS | HEADER_EXTENSIONS),
    }

    if args.by_dir:
        report["by_dir"] = summarize_by_dir(records, args.by_dir)

    return report


def render_text(report: dict[str, object]) -> str:
    cpp = report["cpp"]
    cpp_and_headers = report["cpp_and_headers"]
    assert isinstance(cpp, dict)
    assert isinstance(cpp_and_headers, dict)

    lines = [
        f"root: {report['root']}",
        "excluded dirs: " + ", ".join(report["excluded_dirs"]),
        f"mode: {report['mode']}",
        f".cpp: {cpp['files']} files, {cpp['lines']} lines",
        f".cpp + headers: {cpp_and_headers['files']} files, {cpp_and_headers['lines']} lines",
    ]

    by_dir = report.get("by_dir")
    if isinstance(by_dir, dict) and by_dir:
        lines.append("by dir:")
        for relpath, totals in by_dir.items():
            lines.append(f"  {relpath}: {totals['files']} files, {totals['lines']} lines")

    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    report = build_report(args)

    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print(render_text(report))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

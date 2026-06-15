#!/usr/bin/env python3
import argparse
import re
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslc", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--include-root", required=True)
    parser.add_argument("--contract", required=True)
    parser.add_argument("--generated-source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--depfile", required=True)
    args = parser.parse_args()

    input_path = Path(args.input)
    generated_source = Path(args.generated_source)
    generated_source.parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.depfile).parent.mkdir(parents=True, exist_ok=True)

    source = input_path.read_text(encoding="utf-8")
    include_line = f'#include "{args.contract}"'
    guarded_hook = re.compile(
        r"#if\s+defined\s*\(\s*LX_MATERIAL_CONTRACT_SOURCE\s*\)\s*\n"
        r"\s*#include\s+LX_MATERIAL_CONTRACT_SOURCE\s*\n"
        r"\s*#else\s*\n"
        r"\s*#error[^\n]*\n"
        r"\s*#endif",
        re.MULTILINE,
    )
    source = guarded_hook.sub(include_line, source)
    source = source.replace("#include LX_MATERIAL_CONTRACT_SOURCE",
                            include_line)
    generated_source.write_text(source, encoding="utf-8")

    return subprocess.call([
        args.glslc,
        "-I",
        args.include_root,
        "-MD",
        "-MF",
        args.depfile,
        "-o",
        args.output,
        str(generated_source),
    ])


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["ok", "bad-json", "fail"], default="ok")
    args = parser.parse_args()

    sys.stdin.read()

    if args.mode == "fail":
        print("fake jsonl failure", file=sys.stderr)
        return 9
    if args.mode == "bad-json":
        print("{not json")
        return 0

    print(json.dumps({"type": "assistant_delta", "delta": "exec "}))
    print(json.dumps({"type": "assistant_delta", "delta": "answer"}))
    print(json.dumps({"type": "result", "result": "exec answer"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

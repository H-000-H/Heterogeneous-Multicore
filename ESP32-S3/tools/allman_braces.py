#!/usr/bin/env python3
"""Convert K&R braces to Allman in C/H sources (skip #define / preprocessor)."""

from __future__ import annotations

import re
import sys
from pathlib import Path

STRUCT_RE = re.compile(
    r"^(\s*(?:struct|enum|union)\s+\S+)\s+\{\s*$"
)
TYPEDEF_ENUM_RE = re.compile(r"^(\s*typedef\s+enum\s+\S+)\s+\{\s*$")
INIT_RE = re.compile(r"^(.+=)\s+\{\s*$")
CTRL_RE = re.compile(
    r"^(\s*(?:if|else\s+if|else|for|while|switch|do)\b.*)\s+\{\s*$"
)
FUNC_RE = re.compile(
    r"^(\s*(?:static\s+)?(?:inline\s+)?[\w\s*]+?\([^;]*\))\s+\{\s*$"
)
CASE_RE = re.compile(r"^(\s*case\s+[\w\s]+:)\s+\{\s*$")
EXTERN_C_RE = re.compile(r"^(\s*extern\s+\"C\"\s*)\{\s*$")


def skip_line(stripped: str) -> bool:
    s = stripped.lstrip()
    if s.startswith("#define") and "{" in s:
        return True
    return False


def convert_line(stripped: str, indent: str) -> list[str] | None:
    for pat in (TYPEDEF_ENUM_RE, STRUCT_RE, INIT_RE, EXTERN_C_RE, CTRL_RE, CASE_RE, FUNC_RE):
        m = pat.match(stripped)
        if m:
            return [m.group(1) + "\n", indent + "{\n"]
    return None


def convert_file(path: Path) -> bool:
    raw = path.read_text(encoding="utf-8")
    if "\r\n" in raw:
        newline = "\r\n"
        lines = raw.split("\r\n")
    else:
        newline = "\n"
        lines = raw.split("\n")

    out: list[str] = []
    changed = False

    for line in lines:
        stripped = line.rstrip("\r\n")
        if skip_line(stripped):
            out.append(stripped)
            continue

        indent = re.match(r"^(\s*)", stripped).group(1)
        repl = convert_line(stripped, indent)
        if repl:
            out.extend(s.rstrip("\n") for s in repl)
            changed = True
        else:
            out.append(stripped)

    if changed:
        path.write_text(newline.join(out) + newline, encoding="utf-8")
    return changed


def main(argv: list[str]) -> int:
    roots = argv[1:] if len(argv) > 1 else []
    if not roots:
        print("usage: allman_braces.py <file-or-dir> ...", file=sys.stderr)
        return 1

    files: list[Path] = []
    for root in roots:
        p = Path(root)
        if p.is_file():
            files.append(p)
        else:
            for f in sorted(p.rglob("*.c")) + sorted(p.rglob("*.h")):
                parts = f.parts
                if "build" in parts or "lib" in parts:
                    continue
                files.append(f)

    files = [
        f for f in files
        if "build" not in f.parts and "lib" not in f.parts
    ]

    n = 0
    for f in files:
        if convert_file(f):
            print(f"updated: {f}")
            n += 1
    print(f"done: {n} file(s) updated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

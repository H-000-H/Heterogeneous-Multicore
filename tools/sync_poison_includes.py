#!/usr/bin/env python3
"""Sync compiler_compat_poison.h includes from ESP32-S3 mini_tree to STM32/CH32."""

from __future__ import annotations

import re
from pathlib import Path

ESP32_MT = Path(__file__).resolve().parents[1] / "ESP32-S3" / "components" / "mini_tree"
TARGETS = [
    Path(__file__).resolve().parents[1] / "STM32F407ZGT6" / "mini_tree",
    Path(__file__).resolve().parents[1] / "CH32V307" / "mini_tree",
]

SKIP_PARTS = {"lib", "build", "tools", "vendor"}
POISON_LINE = "#include \"compiler_compat_poison.h\""


def should_skip(rel: Path) -> bool:
    return any(p in SKIP_PARTS for p in rel.parts)


def extract_preamble(esp_text: str) -> list[str]:
    lines = esp_text.splitlines()
    preamble: list[str] = []
    for line in lines:
        if line.startswith("#include"):
            break
        if line.startswith("#define ALLOW_"):
            preamble.append(line)
    return preamble


def insert_preamble(text: str, preamble: list[str]) -> str:
    new_text = text
    for define in preamble:
        if define not in new_text:
            idx = new_text.find("\n")
            if idx == -1:
                new_text = define + "\n" + new_text
            else:
                new_text = new_text[: idx + 1] + define + "\n" + new_text[idx + 1 :]
    return new_text


def add_poison(text: str, rel: Path) -> str:
    if POISON_LINE in text:
        return text

    if rel.name == "printf_output.c":
        text = "#define ALLOW_STDIO_OUTPUT\n\n" + text
        if "#include <stdio.h>" in text:
            return text.replace(
                "#include <stdio.h>\n",
                "#include <stdio.h>\n\n#include \"compiler_compat.h\"\n" + POISON_LINE + "\n",
                1,
            )

    if "#include \"compiler_compat.h\"" in text and "#include \"config.h\"" in text:
        return text.replace(
            "#include \"config.h\"\n",
            "#include \"config.h\"\n" + POISON_LINE + "\n",
            1,
        )

    if "#include \"compiler_compat.h\"" in text:
        return text.replace(
            "#include \"compiler_compat.h\"\n",
            "#include \"compiler_compat.h\"\n" + POISON_LINE + "\n",
            1,
        )

    if "#include \"m_buffer.h\"" in text:
        return text.replace(
            "#include \"m_buffer.h\"\n",
            "#include \"m_buffer.h\"\n" + POISON_LINE + "\n",
            1,
        )

    if rel.parts[0] == "hal_if" and "#include \"hal_cpu.h\"" in text:
        return text.replace(
            "#include \"hal_cpu.h\"\n",
            "#include \"hal_cpu.h\"\n" + POISON_LINE + "\n",
            1,
        )

    if rel.name in ("system_scrubber.c", "system_scrubber.cpp") and "#include \"system_wdt.h\"" in text:
        return text.replace(
            "#include \"system_wdt.h\"\n",
            "#include \"system_wdt.h\"\n" + POISON_LINE + "\n",
            1,
        )

    if rel.name == "production_log.c" and "#include <stdarg.h>" in text:
        return text.replace(
            "#include <stdarg.h>\n",
            "#include <stdarg.h>\n" + POISON_LINE + "\n",
            1,
        )

    if rel.name == "bus.c" and "#include <stddef.h>" in text:
        return text.replace(
            "#include <stddef.h>\n",
            "#include <stddef.h>\n" + POISON_LINE + "\n",
            1,
        )

    if "osal" in rel.parts and rel.name.startswith("osal_") and "#include <stdlib.h>" in text:
        return text.replace(
            "#include <stdlib.h>\n",
            "#include <stdlib.h>\n" + POISON_LINE + "\n",
            1,
        )

    matches = list(re.finditer(r"^#include.*\n", text, re.M))
    if matches:
        pos = matches[-1].end()
        return text[:pos] + POISON_LINE + "\n" + text[pos:]

    return POISON_LINE + "\n" + text


def main() -> None:
    updated: list[str] = []

    for esp_file in ESP32_MT.rglob("*"):
        if esp_file.suffix not in (".c", ".cpp"):
            continue
        rel = esp_file.relative_to(ESP32_MT)
        if should_skip(rel):
            continue

        esp_text = esp_file.read_text(encoding="utf-8")
        if POISON_LINE not in esp_text:
            continue

        preamble = extract_preamble(esp_text)

        for target_root in TARGETS:
            target_file = target_root / rel
            if not target_file.exists():
                continue

            text = target_file.read_text(encoding="utf-8")
            if POISON_LINE in text:
                continue

            new_text = insert_preamble(text, preamble)
            new_text = add_poison(new_text, rel)

            if new_text != text:
                target_file.write_text(new_text, encoding="utf-8")
                updated.append(str(target_file))

    ch32_root = TARGETS[1]
    ch32_only = [
        "hal_if/src/hal_ch32v307.c",
        "hal_if/src/hal_platform_safety.c",
    ]
    for rel_str in ch32_only:
        f = ch32_root / rel_str
        if not f.exists():
            continue
        text = f.read_text(encoding="utf-8")
        if POISON_LINE in text:
            continue
        if rel_str.endswith("hal_ch32v307.c"):
            new_text = text.replace(
                "#include <string.h>\n",
                "#include <string.h>\n" + POISON_LINE + "\n",
                1,
            )
        else:
            new_text = text.replace(
                "#include \"hal_pwm.h\"\n",
                "#include \"hal_pwm.h\"\n" + POISON_LINE + "\n",
                1,
            )
        f.write_text(new_text, encoding="utf-8")
        updated.append(str(f))

    print(f"Updated {len(updated)} files")
    for path in updated:
        print(path)


if __name__ == "__main__":
    main()

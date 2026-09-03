# Extract ```cpp blocks from README.md into a compile-only translation unit.
from __future__ import annotations

import re
import sys
from pathlib import Path

PREAMBLE = r"""// generated from README.md — do not edit
#include <lumen/lumen.h>
#include <lumen/Main.h>
#include <span>
#include <string>
#include <vector>

using namespace lumen;
"""

WRAP_HEAD = """
void readme_snippet_{n}() {{
    App app;
    Window window(L"demo", {{800.0f, 600.0f}});
    bool dirty = false;
    Button* ok = nullptr;
    CommandBar toolbar;
    VectorModel<std::wstring> inbox;
    Command save{{L"save", icon::kSave, L"Ctrl+S", [] {{}}}};
    (void)app; (void)window; (void)dirty; (void)ok; (void)toolbar; (void)inbox; (void)save;
"""


def should_skip(lang: str, body: str) -> bool:
    lang = (lang or "").strip().lower()
    if lang not in {"cpp", "c++", "cxx"}:
        return True
    if "painter." in body or "DrawIcon" in body:
        return True
    if "add_subdirectory" in body or "find_package" in body or "FetchContent" in body:
        return True
    if body.lstrip().startswith("cmake") or "cmake_minimum_required" in body:
        return True
    return False


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: extract_readme.py README.md out.cpp", file=sys.stderr)
        return 2
    src = Path(sys.argv[1]).read_text(encoding="utf-8")
    out = Path(sys.argv[2])
    blocks = re.findall(r"```([^\n]*)\n(.*?)```", src, flags=re.S)
    pieces = [PREAMBLE]
    n = 0
    for lang, body in blocks:
        if should_skip(lang, body):
            continue
        body = body.strip()
        if not body:
            continue
        body = re.sub(r"#include\s*<lumen/[^>]+>\s*", "", body)
        if "lumen_main" in body:
            pieces.append(body.replace("lumen_main", f"readme_main_{n}", 1))
            pieces.append("\n")
        else:
            pieces.append(WRAP_HEAD.format(n=n))
            pieces.append(body)
            if not body.endswith(";"):
                pieces.append(";")
            pieces.append("\n}\n")
        n += 1
    pieces.append(f"\n// {n} snippets\n")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("".join(pieces), encoding="utf-8")
    print(f"extract_readme.py wrote {n} snippets -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

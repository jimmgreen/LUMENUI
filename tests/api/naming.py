# Scan include/lumen/*.h for leftover Get* getters and dual X / SetX writers.
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = ROOT / "include" / "lumen"

# 布局内部、全局钩子、UIA 协议名，以及非双轨动作名。
ALLOW_GET = {
    "GetCurrentProcessId",  # 不应出现
}
ALLOW_SET_PREFIX = {
    "SetBounds",
    "SetChildBounds",
    "SetChildVisibility",
    "SetInterval",
    "SetTimeout",
    "SetLogSink",
    "SetDebugHandler",
    "SetLumaText",
    "SetBackdrop",
    "SetIconWeight",
    "SetButtonHover",
    "SetCaret",
    "SetFlatData",
    "SetTailTarget",
    "SetMode",
    "AutomationSetValue",
    "AutomationSetRange",
}

errors: list[str] = []

get_re = re.compile(r"\bGet([A-Z]\w*)\s*\(")
set_re = re.compile(r"\bSet([A-Z]\w*)\s*\(")
cjk_re = re.compile(r'L"(?:[^"\\]|\\.)*[\u4e00-\u9fff]')


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*?$", "", text, flags=re.M)
    return text


def main() -> int:
    for path in sorted(HEADERS.glob("*.h")):
        if path.name in {"win_undef.h", "wmain.h"}:
            continue
        raw = path.read_text(encoding="utf-8")
        src = strip_comments(raw)
        rel = path.relative_to(ROOT).as_posix()
        for m in get_re.finditer(src):
            name = "Get" + m.group(1)
            if name.startswith("Get") and name not in ALLOW_GET:
                errors.append(f"{rel}: forbidden getter {name}()")
        setters = {m.group(1) for m in set_re.finditer(src)}
        for stem in setters:
            set_name = "Set" + stem
            if set_name in ALLOW_SET_PREFIX or set_name.startswith("AutomationSet"):
                continue
            if re.search(rf"\b{stem}\s*\(", src):
                errors.append(f"{rel}: dual {stem}() and {set_name}()")

    for path in sorted(HEADERS.glob("*.h")):
        if path.name in {"win_undef.h", "wmain.h"}:
            continue
        raw = path.read_text(encoding="utf-8")
        rel = path.relative_to(ROOT).as_posix()
        for tag in ("// Events:", "// Keys:", "// Layout:"):
            if tag not in raw:
                errors.append(f"{rel}: missing {tag}")

    src_root = ROOT / "src"
    for path in sorted(src_root.rglob("*")):
        if path.suffix.lower() not in {".h", ".cpp"}:
            continue
        raw = path.read_text(encoding="utf-8")
        src = strip_comments(raw)
        rel = path.relative_to(ROOT).as_posix()
        for m in cjk_re.finditer(src):
            errors.append(f"{rel}: Chinese literal {m.group(0)}")

    if errors:
        print("naming.py FAIL")
        for e in errors:
            print(" ", e)
        return 1
    print("naming.py OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

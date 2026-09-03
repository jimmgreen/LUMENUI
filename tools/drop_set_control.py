# Rename remaining Control/Panel Set* writers to unprefixed overloads.
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SKIP_DIRS = {"build", ".git", ".zcode", "_deps"}
SKIP_FILES = {
    "优化.md",
    "drop_set_control.py",
    "drop_set_writers.py",
    "naming.py",
}

# Longer names first so SetCardStyle is not split by SetCard.
PAIRS = [
    ("SetCardStyle", "Card"),
    ("SetAccessibleName", "AccessibleName"),
    ("SetContextMenu", "ContextMenu"),
    ("SetSpotlight", "Spotlight"),
    ("SetToolTip", "ToolTip"),
    ("SetVisible", "Visible"),
    ("SetEnabled", "Enabled"),
]

# SetCard( but not SetCardStyle (already rewritten) and not SetCaret.
SETCARD_RE = re.compile(r"\bSetCard(?!Style)\(")


def should_skip(path: Path) -> bool:
    parts = set(path.parts)
    if parts & SKIP_DIRS:
        return True
    if path.name in SKIP_FILES:
        return True
    if path.suffix.lower() not in {".h", ".cpp", ".md", ".py"}:
        return True
    return False


def rewrite(text: str) -> str:
    for old, new in PAIRS:
        text = re.sub(rf"\b{old}\b", new, text)
    text = SETCARD_RE.sub("Card(", text)
    return text


def main() -> None:
    n = 0
    for path in ROOT.rglob("*"):
        if not path.is_file() or should_skip(path):
            continue
        raw = path.read_text(encoding="utf-8")
        out = rewrite(raw)
        if out != raw:
            path.write_text(out, encoding="utf-8", newline="\n")
            n += 1
            print(path.relative_to(ROOT).as_posix())
    print(f"updated {n} files")


if __name__ == "__main__":
    main()

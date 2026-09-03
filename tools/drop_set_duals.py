# Rename leftover dual SetX to X after 10.2.
from pathlib import Path

root = Path(r"C:\Users\SS\Desktop\LUMENUI")
skip_dirs = {"build", ".git", "_deps"}
names = [
    "SetSeriesVisible",
    "SetGroupExpanded",
    "SetItemExpanded",
    "SetItemBadge",
    "SetTabBadge",
    "SetColumnFrozen",
    "SetColumnWidth",
    "SetColumnVisible",
    "SetSearchQuery",
]
# SetAt is special: VectorModel::SetAt -> assign via At(index, value)

exts = {".h", ".cpp"}
for path in root.rglob("*"):
    if any(p in skip_dirs for p in path.parts):
        continue
    if path.suffix not in exts:
        continue
    text = path.read_text(encoding="utf-8")
    orig = text
    for n in names:
        text = text.replace(n, n[3:])  # drop Set
    if text != orig:
        path.write_text(text, encoding="utf-8", newline="\n" if path.suffix != ".md" else None)
        print(path.relative_to(root))

"""Report SHOWBOX UI code that bypasses LUMEN TextRole font sizing."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[3] / "SHOWBOX-LUMEN" / "src" / "Ui"
patterns = (re.compile(r"\.FontSize\s*\("), re.compile(r"\.Font\s*\("),
            re.compile(r"font_size\s*=", re.IGNORECASE))
hits = []
for path in sorted(root.rglob("*.cpp")):
    for line_no, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
        if any(pattern.search(line) for pattern in patterns):
            hits.append(f"{path}:{line_no}:{line.strip()}")
if hits:
    print("SHOWBOX direct font-size overrides:")
    print("\n".join(hits))
    raise SystemExit(1)
print("SHOWBOX direct font-size overrides: none")

# Extract one release section from CHANGELOG.md as the GitHub release body.
from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: extract_changelog.py <version> <out.md>", file=sys.stderr)
        return 2
    version, out_path = sys.argv[1], Path(sys.argv[2])
    lines = Path("CHANGELOG.md").read_text(encoding="utf-8").splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.startswith("## ") and version in line:
            start = i + 1
            break
    if start is None:
        print(f"CHANGELOG.md has no section for {version}", file=sys.stderr)
        return 1
    body: list[str] = []
    for line in lines[start:]:
        if line.startswith("## "):
            break
        body.append(line)
    out_path.write_text("\n".join(body).strip() + "\n", encoding="utf-8")
    print(f"extract_changelog.py wrote {version} -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

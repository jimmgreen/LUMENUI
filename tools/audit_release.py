"""Reject private paths and common credential markers in release ZIP contents."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from zipfile import ZipFile


PATTERNS = {
    "user directory": re.compile(rb"(?i)(?:[a-z]:[\\/]+Users[\\/]+|/Users/|/home/)"),
    "credential marker": re.compile(
        rb"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}"
        rb"|AKIA[0-9A-Z]{16}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)"
    ),
}


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: audit_release.py <archive.zip> [...]")
        return 2
    failures = 0
    for archive in map(Path, sys.argv[1:]):
        with ZipFile(archive) as package:
            for entry in package.infolist():
                if entry.is_dir():
                    continue
                data = entry.filename.encode("utf-8") + b"\n" + package.read(entry)
                # Inspect ASCII and UTF-16 strings; never print matched values.
                for label, pattern in PATTERNS.items():
                    if pattern.search(data) or pattern.search(data.replace(b"\0", b"")):
                        print(f"FAIL {archive.name}: {entry.filename}: {label}")
                        failures += 1
        print(f"Scanned {archive.name}")
    if failures:
        print(f"Release blocked: {failures} sensitive-content findings")
        return 1
    print("Release privacy pattern check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

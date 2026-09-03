# Rename leftover Set* property writers to unprefixed overloads. No aliases.
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {".git", "build", ".zcode", "_deps"}
SKIP_NAMES = {"优化.md", "drop_set_writers.py"}

# Tokens that never collide with Painter / Automation / Win32.
GLOBAL = [
    "SetItemCount",
    "SetRowCount",
    "SetGroups",
    "SetItemEnabled",
    "SetItemChecked",
    "SetActiveColumn",
    "SetRowHeight",
    "SetColumnKind",
    "SetColumnAggregate",
    "SetGlowIntensity",
]


def keep(path: Path) -> bool:
    if path.name in SKIP_NAMES:
        return False
    parts = set(path.parts)
    if parts & SKIP_DIRS:
        return False
    if "fluentui" in parts:
        return False
    return path.suffix.lower() in {".h", ".cpp", ".md"} and "include" in str(path) or path.suffix.lower() in {
        ".h",
        ".cpp",
        ".md",
        ".py",
    }


def rewrite(text: str, path: Path) -> str:
    rel = path.relative_to(ROOT).as_posix()
    for old in GLOBAL:
        new = old[3:]  # drop Set
        text = text.replace(old, new)

    # Slider / RangeSlider range; never AutomationSetRange
    text = re.sub(r"(?<!Automation)SetRange\b", "Range", text)

    # Chart / Sparkline / RangeSlider
    text = text.replace("SetCount", "Count")
    text = text.replace("SetValues", "Values")

    # StatusBar item fields
    if "StatusBar" in text or rel.endswith("visual/main.cpp"):
        text = text.replace("StatusBar::SetText", "StatusBar::ItemText")
        text = text.replace("StatusBar::SetGlyph", "StatusBar::ItemGlyph")
        text = re.sub(r"StatusBar&\s+SetText\(", "StatusBar& ItemText(", text)
        text = re.sub(r"StatusBar&\s+SetGlyph\(", "StatusBar& ItemGlyph(", text)
        # visual test: bar.SetText(
        if rel.endswith("visual/main.cpp"):
            text = text.replace("bar.SetText(", "bar.ItemText(")

    # Button height (not TitleBar::Height const)
    text = text.replace("Button::SetHeight", "Button::Height")
    text = re.sub(r"Button&\s+SetHeight\(", "Button& Height(", text)
    text = text.replace(".SetHeight(", ".Height(")
    text = text.replace("RepeatButton& SetHeight", "RepeatButton& Height")

    # TitleBar chrome
    text = text.replace("TitleBar::SetMaximized", "TitleBar::Maximized")
    text = text.replace("void SetMaximized(", "void Maximized(")
    text = text.replace("->SetMaximized(", "->Maximized(")

    # Window public only. Painter::SetBackdrop 与 WindowImpl::SetBackdrop 留下。
    if rel == "include/lumen/Window.h":
        text = text.replace("void SetBackdrop(Backdrop", "void Backdrop(Backdrop")
        text = text.replace("void SetTrayMenu(Menu", "void TrayMenu(Menu")
    if rel in {
        "src/core/window_impl.cpp",
        "examples/gallery/main.cpp",
        "examples/template/main.cpp",
        "tests/anim/main.cpp",
        "README.md",
        "examples/gallery/section_overview.cpp",
        "examples/gallery/section_selection.cpp",
        "examples/gallery/common.cpp",
        "AGENTS.md",
    }:
        text = text.replace("void Window::SetBackdrop", "void Window::Backdrop")
        text = text.replace("window.SetBackdrop", "window.Backdrop")
        text = text.replace("Window::SetBackdrop", "Window::Backdrop")
        text = text.replace("void Window::SetTrayMenu", "void Window::TrayMenu")
        text = text.replace("Window::SetTrayMenu", "Window::TrayMenu")
        text = text.replace(".SetTrayMenu(", ".TrayMenu(")

    return text


def main() -> None:
    n = 0
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(p in SKIP_DIRS for p in path.parts):
            continue
        if path.suffix.lower() not in {".h", ".cpp", ".md"}:
            continue
        if "fluentui" in path.parts or path.name == "优化.md":
            continue
        old = path.read_text(encoding="utf-8")
        new = rewrite(old, path)
        if new != old:
            path.write_text(new, encoding="utf-8", newline="\n")
            n += 1
            print(path.relative_to(ROOT).as_posix())
    print(f"updated {n} files")


if __name__ == "__main__":
    main()

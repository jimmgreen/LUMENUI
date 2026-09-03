# Convert remaining assignment-style OnX to Signal. One-shot.
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# header relative, member, Signal type args inside < >
TARGETS = [
    ("include/lumen/Flyout.h", "closed_", ""),
    ("include/lumen/Drawer.h", "closed_", ""),
    ("include/lumen/TeachingTip.h", "closed_", ""),
    ("include/lumen/InfoBar.h", "closed_", ""),
    ("include/lumen/Expander.h", "changed_", ""),
    ("include/lumen/Swatch.h", "picked_", ""),
    ("include/lumen/HotkeyBox.h", "changed_", ""),
    ("include/lumen/TokenBox.h", "changed_", ""),
    ("include/lumen/DropDownButton.h", "dropdown_", ""),
    ("include/lumen/SplitButton.h", "dropdown_", ""),
    ("include/lumen/SplitButton.h", "toggled_", "bool"),
    ("include/lumen/SplitView.h", "toggled_", "bool"),
    ("include/lumen/Splitter.h", "dragged_", "float"),
    ("include/lumen/Splitter.h", "ended_", ""),
    ("include/lumen/StatusBar.h", "invoked_", "std::wstring_view"),
    ("include/lumen/Breadcrumb.h", "navigate_", "size_t"),
    ("include/lumen/Carousel.h", "page_changed_", "size_t"),
    ("include/lumen/Pagination.h", "navigate_", "size_t"),
    ("include/lumen/Rating.h", "rated_", "int"),
    ("include/lumen/Stepper.h", "step_changed_", "size_t"),
    ("include/lumen/FileDropZone.h", "dropped_", "const std::vector<std::wstring>&"),
    ("include/lumen/ListView.h", "group_expanded_changed_", "std::wstring_view, bool"),
    ("include/lumen/ListView.h", "reordered_", "size_t, size_t"),
    ("include/lumen/Table.h", "sort_changed_", "int, int"),
    ("include/lumen/Table.h", "cell_edited_", "size_t, int, std::wstring"),
    ("include/lumen/Table.h", "frozen_changed_", "int, bool"),
    ("include/lumen/TabControl.h", "closed_", "std::wstring_view"),
    ("include/lumen/TabControl.h", "reordered_", "size_t, size_t"),
]


def signal_type(args: str) -> str:
    return "Signal<>" if not args else f"Signal<{args}>"


def main() -> None:
    for rel, member, args in TARGETS:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        orig = text
        text = text.replace(f"{member} = std::move(handler)", f"{member}.Subscribe(std::move(handler))")
        sig = signal_type(args)
        text = re.sub(
            rf"std::function<void\([^;]*\)>\s+{member};",
            f"{sig} {member};",
            text,
        )
        # some are split across lines already Signal-ready
        if "Signal.h" not in text and '#include "Signal.h"' not in text:
            text = text.replace('#include "ControlOf.h"', '#include "ControlOf.h"\n#include "Signal.h"', 1)
            if '#include "Signal.h"' not in text:
                text = text.replace('#include "Panel.h"', '#include "Panel.h"\n#include "Signal.h"', 1)
        if text != orig:
            path.write_text(text, encoding="utf-8", newline="\n")
            print("header", rel)

    # cpp emit
    pat_call = re.compile(r"if \((\w+)\) \1\(([^;]*)\);")
    pat_void = re.compile(r"if \((\w+)\) \1\(\);")
    members = {m for _, m, _ in TARGETS}
    for folder in ("src/controls", "src/core"):
        for path in (ROOT / folder).rglob("*.cpp"):
            text = path.read_text(encoding="utf-8")
            orig = text

            def repl_call(m: re.Match[str]) -> str:
                name = m.group(1)
                if name not in members:
                    return m.group(0)
                args = m.group(2)
                return f"{name}.Emit({args});"

            def repl_void(m: re.Match[str]) -> str:
                name = m.group(1)
                if name not in members:
                    return m.group(0)
                return f"{name}.Emit();"

            text = pat_call.sub(repl_call, text)
            text = pat_void.sub(repl_void, text)
            if text != orig:
                path.write_text(text, encoding="utf-8", newline="\n")
                print("cpp", path.relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()

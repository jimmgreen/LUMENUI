# One-shot: ControlOf/PanelOf inheritance + strip Control/Panel setter forwards.
from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1] / "include" / "lumen"

SKIP_INHERIT = {
    "Control.h",
    "ControlOf.h",
    "Panel.h",
    "RepeatButton.h",
    "PasswordBox.h",
    "NumberBox.h",
    "AutoSuggestBox.h",
    "BusyOverlay.h",
    "Drawer.h",
    "ToolTip.h",
    "Flyout.h",
    "EmptyState.h",
}

FORWARD_NAME = (
    r"ToolTip|Enabled|Grow|FillCross|MinSize|MaxSize|Margin|Style|Density|"
    r"ContextMenu|Spotlight|Visible|Clip"
)


def is_pure_forward(body: str) -> bool:
    body = re.sub(r"//[^\n]*", "", body)
    for raw in body.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line == "return *this;":
            continue
        if re.match(
            r"(?:Control|Panel|Button)::\w+\s*\(.*\)\s*;",
            line,
        ):
            continue
        return False
    return True


def strip_forwards(text: str) -> str:
    text = re.sub(r"\n    // 基类链式转发：[^\n]*", "", text)
    text = re.sub(
        r"\n    \w+& ToolTip\(std::unique_ptr<class ToolTip> content\);",
        "",
        text,
    )

    def repl_fn(match: re.Match[str]) -> str:
        body = match.group(1)
        if is_pure_forward(body):
            return ""
        return match.group(0)

    text = re.sub(
        rf"\n    \w+& (?:{FORWARD_NAME})\([^;{{]*\)\s*\{{([^{{}}]*)\}}",
        repl_fn,
        text,
        flags=re.DOTALL,
    )
    text = re.sub(
        r"\n    template <typename\.\.\. Ts>\n"
        r"    \w+& Children\(Ts&&\.\.\. xs\) \{\n"
        r"        Panel::Children\(std::forward<Ts>\(xs\)\.\.\.\);\n"
        r"        return \*this;\n"
        r"    \}",
        "",
        text,
    )
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text


def convert_file(path: pathlib.Path) -> None:
    original = path.read_text(encoding="utf-8")
    text = original
    name = path.name

    if name not in SKIP_INHERIT:
        m = re.search(r"^class (\w+) : public Control \{", text, re.M)
        if m:
            cls = m.group(1)
            text = re.sub(
                rf"^class {cls} : public Control \{{",
                f"class {cls} : public ControlOf<{cls}> {{",
                text,
                count=1,
                flags=re.M,
            )
            if '#include "Control.h"' in text:
                text = text.replace('#include "Control.h"', '#include "ControlOf.h"', 1)
            elif '#include "ControlOf.h"' not in text:
                text = re.sub(
                    r'(#include "[^"]+"\n)+',
                    lambda m: m.group(0)
                    if '"ControlOf.h"' in m.group(0)
                    else m.group(0) + '#include "ControlOf.h"\n',
                    text,
                    count=1,
                )

        m = re.search(r"^class (\w+) : public Panel \{", text, re.M)
        if m:
            cls = m.group(1)
            text = re.sub(
                rf"^class {cls} : public Panel \{{",
                f"class {cls} : public PanelOf<{cls}> {{",
                text,
                count=1,
                flags=re.M,
            )

    text = strip_forwards(text)
    if text != original:
        path.write_text(text, encoding="utf-8", newline="\n")
        print("updated", name)


def main() -> None:
    for path in sorted(ROOT.glob("*.h")):
        convert_file(path)


if __name__ == "__main__":
    main()

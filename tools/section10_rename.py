# One-shot: drop Set*/Get* aliases and retarget call sites. Not part of the build.
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def iter_sources() -> list[Path]:
    out: list[Path] = []
    for folder in ("include/lumen", "src", "examples", "tests"):
        base = ROOT / folder
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if p.suffix.lower() not in {".h", ".cpp"}:
                continue
            if "fluentui" in p.relative_to(ROOT).as_posix():
                continue
            out.append(p)
    return out


def patch_text(path: Path, text: str) -> str:
    rel = path.relative_to(ROOT).as_posix()

    skip_value = rel in {
        "src/core/painter.cpp",
        "src/core/uia.cpp",
        "src/core/settings.cpp",
        "src/core/window_impl.cpp",
    }
    skip_size = rel in {
        "src/core/renderer.cpp",
        "src/core/offscreen.cpp",
        "src/controls/image_view.cpp",
        "src/controls/title_bar.cpp",
        "src/core/uia.cpp",
    }

    ident = [
        ("SetSelectedIndices", "SelectedIndices"),
        ("SetSelectedIndex", "SelectedIndex"),
        ("SetSelectedId", "SelectedId"),
        ("SetIndeterminate", "Indeterminate"),
        ("SetChecked", "Checked"),
        ("SetState", "State"),
        ("GetOrientation", "Orientation"),
        ("SetOrientation", "Orientation"),
        ("GetFuture", "Future"),
    ]
    for old, new in ident:
        text = re.sub(rf"\b{old}\b", new, text)

    # Public call sites; WindowImpl keeps SetToastMotion internally (enum vs method).
    if rel not in {"src/core/window_impl.cpp", "src/core/window_impl.h", "src/core/overlay_host.cpp"}:
        text = text.replace(".SetToastMotion(", ".ToastMotion(")
        text = text.replace("->SetToastMotion(", "->ToastMotion(")
        text = text.replace(".GetToastMotion()", ".ToastMotion()")
        text = text.replace("->GetToastMotion()", "->ToastMotion()")
    if rel == "src/core/window_impl.cpp":
        text = text.replace(
            "void Window::SetToastMotion(lumen::ToastMotion motion)",
            "void Window::ToastMotion(lumen::ToastMotion motion)",
        )
        text = text.replace(
            "lumen::ToastMotion Window::GetToastMotion() const",
            "lumen::ToastMotion Window::ToastMotion() const",
        )

    if not skip_value:
        text = re.sub(r"\bSetValue\b", "Value", text)

    text = re.sub(r"\bSetSelected\b", "Selected", text)

    text = text.replace(".SetSize(ButtonSize", ".SizeClass(ButtonSize")
    text = text.replace("->SetSize(ButtonSize", "->SizeClass(ButtonSize")
    text = re.sub(r"\bSetSize\s*\(\s*ButtonSize", "SizeClass(ButtonSize", text)

    if rel != "src/core/uia.cpp":
        # Drop the Control alias; rewrite other SetFocus() calls to Focus().
        text = re.sub(
            r"^[ \t]*void SetFocus\(\) \{ Focus\(\); \}\n",
            "",
            text,
            flags=re.M,
        )
        text = re.sub(r"\bSetFocus\s*\(", "Focus(", text)

    # No-arg event lambdas need the payload the remaining overload requires.
    text = re.sub(
        r"OnSelectionChanged\(\[([^\]]*)\]\s*\{",
        r"OnSelectionChanged([\1](ptrdiff_t, ptrdiff_t) {",
        text,
    )
    text = re.sub(
        r"OnActivate\(\[([^\]]*)\]\s*\{",
        r"OnActivate([\1](size_t) {",
        text,
    )
    text = re.sub(
        r"OnTextChanged\(\[([^\]]*)\]\s*\{",
        r"OnTextChanged([\1](std::wstring_view) {",
        text,
    )
    return text


def strip_alias_lines(text: str) -> str:
    patterns = [
        r"^[ \t]*ButtonSize GetSize\(\) const noexcept \{ return SizeClass\(\); \}\n",
        r"^[ \t]*[A-Za-z0-9_]+& SetSize\(ButtonSize value\) \{ return SizeClass\(value\); \}\n",
        r"^[ \t]*[A-Za-z0-9_]+& Value\(float value\) \{ return Value\(value\); \}\n",
        r"^[ \t]*[A-Za-z0-9_]+& Checked\(bool value\) \{ return Checked\(value\); \}\n",
        r"^[ \t]*[A-Za-z0-9_]+& Selected\(bool value\) \{ return Selected\(value\); \}\n",
        r"^[ \t]*Splitter& Orientation\(Orientation value\) \{ return Orientation\(value\); \}\n",
    ]
    for p in patterns:
        text = re.sub(p, "", text, flags=re.M)
    return text


def strip_void_event_overloads(text: str) -> str:
    block = re.compile(
        r"^[ \t]*[A-Za-z0-9_]+& On(?:SelectionChanged|Activate|TextChanged)\(std::function<void\(\)> handler\) \{\n"
        r"(?:^[ \t]+.*\n)+?"
        r"^[ \t]*\}\n",
        re.M,
    )
    return block.sub("", text)


def patch_window_header(text: str) -> str:
    text = re.sub(
        r"^[ \t]*void SetToastMotion\(ToastMotion motion\);\n"
        r"^[ \t]*ToastMotion GetToastMotion\(\) const;\n"
        r"^[ \t]*lumen::ToastMotion ToastMotion\(\) const \{ return GetToastMotion\(\); \}\n"
        r"^[ \t]*void ToastMotion\(lumen::ToastMotion motion\) \{ SetToastMotion\(motion\); \}\n",
        "    lumen::ToastMotion ToastMotion() const;\n"
        "    void ToastMotion(lumen::ToastMotion motion);\n",
        text,
        flags=re.M,
    )
    return text


def main() -> None:
    for path in iter_sources():
        original = path.read_text(encoding="utf-8")
        rel = path.relative_to(ROOT).as_posix()
        text = original
        if rel == "include/lumen/Window.h":
            text = patch_window_header(text)
        text = patch_text(path, text)
        text = strip_alias_lines(text)
        text = strip_void_event_overloads(text)
        if rel == "tests/visual/main.cpp":
            text = text.replace(
                "void Focus() { Focus(); }",
                "void Focus() { ComboBox::Focus(); }",
            )
        if text != original:
            path.write_text(text, encoding="utf-8", newline="\n")
            print("updated", rel)


if __name__ == "__main__":
    main()

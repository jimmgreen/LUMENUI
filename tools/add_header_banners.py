# Ensure public headers carry // Events: / // Keys: / // Layout: banners.
import re
from pathlib import Path

root = Path(r"C:\Users\SS\Desktop\LUMENUI\include\lumen")
skip = {"win_undef.h"}

protected_on = {
    "OnMouseEnter", "OnMouseLeave", "OnMouseMove", "OnMouseDown", "OnMouseUp",
    "OnMouseDoubleClick", "OnWheel", "OnHWheel", "OnKey", "OnChar", "OnFocusChanged",
    "OnAnimate", "OnImeCompose", "OnImeCommit", "OnImeEnd", "OnFileDrag", "OnFileDrop",
    "OnTextDrop", "OnClick",  # if it's the virtual? Panel OnClick is public event
}

def events_line(text: str) -> str:
    names = []
    for m in re.finditer(r"\b(On[A-Z]\w*|Bind[A-Z]\w*)\s*\(\s*std::function", text):
        n = m.group(1)
        if n in ("OnMouseEnter", "OnMouseLeave", "OnMouseMove", "OnMouseDown", "OnMouseUp",
                 "OnMouseDoubleClick", "OnWheel", "OnHWheel", "OnKey", "OnChar",
                 "OnFocusChanged", "OnAnimate"):
            continue
        if n not in names:
            names.append(n)
    return " / ".join(names) if names else "无（本头无订阅事件）"

def keys_line(text: str) -> str:
    if re.search(r"\bbool OnKey\(", text):
        return "焦点控件处理 Enter/Space/方向键等，详见 OnKey"
    if "Focusable() const noexcept override { return true; }" in text:
        return "可聚焦，参与 Tab 环"
    return "无独立快捷键（命中穿透或非焦点）"

def layout_line(text: str) -> str:
    if "ControlOf<" in text or "PanelOf<" in text:
        return "Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure"
    if "class Window" in text:
        return "顶层窗口，客户区由 Root() 布局"
    return "非布局控件头，或见类声明"

changed = 0
for path in sorted(root.glob("*.h")):
    if path.name in skip:
        continue
    src = path.read_text(encoding="utf-8")
    if "// Events:" in src and "// Keys:" in src and "// Layout:" in src:
        continue
    ev = events_line(src)
    ky = keys_line(src)
    ly = layout_line(src)
    banner = f"// Events: {ev}\n// Keys: {ky}\n// Layout: {ly}\n"
    lines = src.splitlines(True)
    insert_at = 0
    if lines and lines[0].startswith("//"):
        insert_at = 1
        while insert_at < len(lines) and lines[insert_at].startswith("//") and not lines[insert_at].startswith("// Events:"):
            # keep the one-line file title only
            break
    lines.insert(insert_at, banner)
    path.write_text("".join(lines), encoding="utf-8", newline="\n")
    changed += 1
    print("banner", path.name)
print("updated", changed)

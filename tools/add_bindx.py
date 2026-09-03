# Add BindX next to OnX that already Subscribe a Signal, if BindX is missing.
import re
from pathlib import Path

root = Path(r"C:\Users\SS\Desktop\LUMENUI\include\lumen")
skip = {"Signal.h", "Control.h", "ControlOf.h", "win_undef.h", "lumen.h"}

def insert_binds(text: str) -> str:
    out = []
    i = 0
    while i < len(text):
        m = re.search(
            r'(?P<indent>[ \t]+)(?P<cls>[\w:]+)& On(?P<name>\w+)\((?P<sig>std::function<[^;{]+?>) handler\)',
            text[i:],
        )
        if not m:
            out.append(text[i:])
            break
        start = i + m.start()
        out.append(text[i:start])
        rest = text[i + m.start() :]
        brace = rest.find("{")
        if brace < 0 or brace > 200:
            # declaration only
            semi = rest.find(";")
            chunk = rest[: semi + 1]
            out.append(chunk)
            i = i + m.start() + semi + 1
            continue
        # match body
        depth = 0
        j = brace
        while j < len(rest):
            if rest[j] == "{":
                depth += 1
            elif rest[j] == "}":
                depth -= 1
                if depth == 0:
                    j += 1
                    break
            j += 1
        method = rest[:j]
        name = m.group("name")
        sig = m.group("sig")
        indent = m.group("indent")
        out.append(method)
        i = i + m.start() + j
        if "Subscribe(" not in method:
            continue
        bind_name = f"Bind{name}"
        # already present nearby (next 400 chars of remaining original, or already in file)
        lookahead = text[i : i + 400]
        if bind_name + "(" in lookahead or f" {bind_name}(" in text:
            # if Bind exists anywhere in this file for this event, skip
            if re.search(rf'\b{bind_name}\(', text):
                continue
        sm = re.search(r"(\w+)\.Subscribe\(std::move\(handler\)\)", method)
        if not sm:
            continue
        signal = sm.group(1)
        bind = (
            f"\n{indent}Connection {bind_name}({sig} handler) {{\n"
            f"{indent}    return {signal}.Connect(std::move(handler));\n"
            f"{indent}}}"
        )
        out.append(bind)
    return "".join(out)

changed = 0
for path in sorted(root.glob("*.h")):
    if path.name in skip:
        continue
    src = path.read_text(encoding="utf-8")
    dst = insert_binds(src)
    if dst != src:
        path.write_text(dst, encoding="utf-8", newline="\n")
        changed += 1
        print("bindx", path.name)
print("updated", changed)

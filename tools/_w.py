from pathlib import Path

# TitleBar::Status skip no-op invalidate
p = Path(r"C:\Users\SS\Desktop\LUMENUI\include\lumen\TitleBar.h")
t = p.read_text(encoding="utf-8")
old = """    TitleBar& Status(std::wstring_view text) {
        status_ = text;
        Invalidate();
        return *this;
    }
"""
new = """    TitleBar& Status(std::wstring_view text) {
        if (status_ == text) return *this;
        status_ = text;
        Invalidate();
        return *this;
    }
"""
if old not in t:
    raise SystemExit("Status block not found")
p.write_text(t.replace(old, new, 1), encoding="utf-8", newline="\n")

# UpdatePerfHud only push on change
p = Path(r"C:\Users\SS\Desktop\LUMENUI\src\core\window_impl.cpp")
t = p.read_text(encoding="utf-8")
if "#include <cstdio>" not in t:
    t = t.replace("#include <vector>", "#include <vector>\n#include <cstdio>\n#include <typeinfo>")
old = """    if (wcscmp(perf_hud_, buf) != 0) wcscpy_s(perf_hud_, buf);
    if (title_bar_) title_bar_->Status(perf_hud_);
"""
new = """    if (wcscmp(perf_hud_, buf) != 0) {
        wcscpy_s(perf_hud_, buf);
        if (title_bar_) title_bar_->Status(perf_hud_);
    }
"""
if old not in t:
    raise SystemExit("perf hud push not found")
t = t.replace(old, new, 1)

# Insert dump after Layout arrange of root
old = """    Control& root_control = *root_;
    root_control.Measure({w, content_h}, theme_);
    root_control.Arrange({0.0f, chrome, w, content_h});
"""
new = r"""    Control& root_control = *root_;
    root_control.Measure({w, content_h}, theme_);
    root_control.Arrange({0.0f, chrome, w, content_h});
    {
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            FILE* f = nullptr;
            fopen_s(&f, "C:\\Users\\SS\\Desktop\\LUMENUI\\build\\hit.log", "w");
            if (f) {
                fprintf(f, "scale=%.3f caption=%.1f client=%.0fx%.0f chrome=%.1f\n",
                        scale_, chrome, w, h, chrome);
                auto dump = [&](auto& self, Control* c, int depth) -> void {
                    if (!c) return;
                    const Rect& a = c->absolute_;
                    fprintf(f, "%*s%s vis=%d en=%d ht=%d abs=(%.1f,%.1f %.1fx%.1f)\n",
                            depth * 2, "", typeid(*c).name(), (int)c->visible_, (int)c->enabled_,
                            (int)c->HitTransparent(), a.x, a.y, a.w, a.h);
                    if (auto* panel = dynamic_cast<Panel*>(c)) {
                        for (size_t i = 0; i < panel->ChildCount(); ++i) {
                            self(self, &panel->Child(i), depth + 1);
                        }
                    }
                };
                fprintf(f, "-- title_bar --\n");
                dump(dump, title_bar_.get(), 0);
                fprintf(f, "-- root --\n");
                dump(dump, root_.get(), 0);
                auto probe = [&](float x, float y, const char* label) {
                    Control* hit = HitTest({x, y});
                    fprintf(f, "HitTest %s (%.1f,%.1f) -> %s abs=(%.1f,%.1f %.1fx%.1f)\n",
                            label, x, y, hit ? typeid(*hit).name() : "null",
                            hit ? hit->absolute_.x : 0, hit ? hit->absolute_.y : 0,
                            hit ? hit->absolute_.w : 0, hit ? hit->absolute_.h : 0);
                };
                probe(w * 0.5f, 20.0f, "caption-mid");
                probe(w * 0.5f, 80.0f, "y80");
                probe(w * 0.5f, h * 0.5f, "center");
                probe(80.0f, 80.0f, "y80-left");
                fclose(f);
            }
        }
    }
"""
if old not in t:
    raise SystemExit("layout arrange block not found")
t = t.replace(old, new, 1)
p.write_text(t, encoding="utf-8", newline="\n")
print("patched window_impl + TitleBar Status")

# Splitter measure cap
p = Path(r"C:\Users\SS\Desktop\LUMENUI\src\controls\splitter.cpp")
t = p.read_text(encoding="utf-8")
old = """Size Splitter::Measure(Size available, const Theme&) {
    if (orientation_ == Orientation::Vertical) {
        return {thickness_, available.h > 0.0f ? available.h : 24.0f};
    }
    return {available.w > 0.0f ? available.w : 24.0f, thickness_};
}
"""
new = """Size Splitter::Measure(Size available, const Theme&) {
    const auto along = [](float v) { return (v > 0.0f && v < 1.0e4f) ? v : 24.0f; };
    if (orientation_ == Orientation::Vertical) {
        return {thickness_, along(available.h)};
    }
    return {along(available.w), thickness_};
}
"""
if old not in t:
    raise SystemExit("splitter measure not found")
p.write_text(t.replace(old, new, 1), encoding="utf-8", newline="\n")
print("splitter measure capped")

from pathlib import Path
root = Path(r"C:\Users\SS\Desktop\LUMENUI")

def patch(rel, old, new):
    p = root / rel
    t = p.read_text(encoding="utf-8")
    if old not in t:
        raise SystemExit(f"MISSING in {rel}:\n{old[:240]!r}")
    p.write_text(t.replace(old, new, 1), encoding="utf-8", newline="\n")
    print(f"OK {rel}")

canvas = r'''
constexpr char kCirclePath[] =
    "M128,24A104,104,0,1,0,232,128,104.11,104.11,0,0,0,128,24Zm0,192a88,88,0,1,1,88-88A88.1,88.1,0,0,1,128,216Z";
constexpr wchar_t kCircleGlyph[] = L"\uF000";

struct CircleReg {
    CircleReg() { lumen::icon::Register(kCircleGlyph[0], kCirclePath); }
};
const CircleReg g_circle_reg;

class PainterIconStrip : public lumen::Control {
protected:
    lumen::Size Measure(lumen::Size, const lumen::Theme&) override { return {120.0f, 28.0f}; }
    void Draw(lumen::Painter& painter, const lumen::Theme& theme) override {
        using namespace lumen;
        const float s = icon::kSize;
        float x = absolute_.x;
        const float y = absolute_.y + (absolute_.h - s) * 0.5f;
        painter.DrawIcon(icon::kSearch, {x, y, s, s}, theme.text);
        x += s + 12.0f;
        painter.DrawIcon(icon::kZap, Point{x + s * 0.5f, absolute_.y + absolute_.h * 0.5f},
                         theme.text);
        x += s + 12.0f;
        painter.DrawIconPath(kCirclePath, {x, y, s, s}, theme.text);
    }
    bool HitTransparent() const noexcept override { return true; }
};

'''

patch("examples/gallery/section_content.cpp",
"""struct IconPlayground {
""",
canvas + """struct IconPlayground {
""")

patch("examples/gallery/section_content.cpp",
"""    auto& body = Showcase(column, L"ICONS \\u00B7 Phosphor");
    body.Spacing(16.0f);

    auto& head = body.Add<Row>().Spacing(20.0f).AlignCross(Cross::Center);
""",
"""    auto& body = Showcase(column, L"ICONS \\u00B7 Phosphor");
    body.Spacing(16.0f);

    body.Add<Label>(L"PAINTER \\u00B7 DrawIcon / DrawIconPath / icon::Register",
                    TextRole::CaptionStrong)
        .Secondary(true);
    auto& api = body.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    api.Add<PainterIconStrip>();
    api.Add<IconView>(kCircleGlyph)
        .Box(28.0f)
        .IconSize(icon::kSize)
        .Weight(icon::kWeight)
        .CornerRadius(8.0f)
        .Background(Color{0.0f, 0.0f, 0.0f, 0.0f})
        .Stroke(Color{0.0f, 0.0f, 0.0f, 0.0f})
        .ToolTip(L"Registered F000 circle");
    api.Add<Label>(L"DrawIcon(glyph, slot, color) defaults 16 / 1.5. Register a 256-viewBox SVG d "
                   L"to reuse IconView / Button::Glyph.",
                   TextRole::Caption)
        .Secondary(true)
        .Wrap(true)
        .Grow();

    auto& head = body.Add<Row>().Spacing(20.0f).AlignCross(Cross::Center);
""")

patch("README.md",
"""图标使用系统字体 Segoe Fluent Icons 字形（`lumen::icon::kSettings` 等，Win10 自动回退 Segoe MDL2 Assets），无资源文件。
""",
"""图标按 Phosphor Regular 路径绘制（`lumen::icon::kSettings` 等，默认 16px / weight 1.5）。自定义控件里：

```cpp
painter.DrawIcon(lumen::icon::kSearch, slot, theme.text);          // 默认 16 / 1.5
painter.DrawIcon(lumen::icon::kZap, center, theme.text, 20.0f);    // 点居中
painter.DrawIconPath(kSvgD, slot, theme.text);                     // 一次性 SVG（viewBox 256）
lumen::icon::Register(L'\\uF000'[0], kSvgD);                       // 挂到字形，IconView / Button 也能画
```

未注册的码点回退 Segoe Fluent / MDL2。无资源文件。
""")
print("gallery+readme done")

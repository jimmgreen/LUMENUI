#include "fluentui/Painter.h"
#include "lumatext_bridge.h"
#include "text_service.h"
#include <cmath>
#include <vector>

namespace fui {
namespace {

D2D1_COLOR_F ToD2D(Color c) { return {c.r, c.g, c.b, c.a}; }
D2D1_RECT_F ToD2D(const Rect& r) { return {r.x, r.y, r.Right(), r.Bottom()}; }

uint32_t PackColor(Color c) {
    auto q = [](float v) {
        uint32_t x = static_cast<uint32_t>(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        return x > 255 ? 255u : x;
    };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

float Snap(float value, float scale) {
    return std::floor(value * scale + 0.5f) / scale;
}

DWRITE_TEXT_ALIGNMENT MapAlign(Align align) {
    switch (align) {
    case Align::Center: return DWRITE_TEXT_ALIGNMENT_CENTER;
    case Align::Trailing: return DWRITE_TEXT_ALIGNMENT_TRAILING;
    default: return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

} // namespace

Painter::~Painter() {
    for (auto& entry : brushes_) entry.second->Release();
    brushes_.clear();
    if (round_stroke_) round_stroke_->Release();
}

void Painter::BeginFrame(ID2D1DeviceContext2* dc, TextService* text, float scale) {
    if (dc != dc_) {
        for (auto& entry : brushes_) entry.second->Release();
        brushes_.clear();
        dc_ = dc;
    }
    text_ = text;
    scale_ = scale;
    if (!dc_) return;
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

ID2D1SolidColorBrush* Painter::Brush(Color color) {
    const uint32_t key = PackColor(color);
    auto it = brushes_.find(key);
    if (it != brushes_.end()) return it->second;
    if (brushes_.size() >= 320) {
        for (auto& entry : brushes_) entry.second->Release();
        brushes_.clear();
    }
    ID2D1SolidColorBrush* brush = nullptr;
    if (FAILED(dc_->CreateSolidColorBrush(ToD2D(color), &brush))) return nullptr;
    brushes_.emplace(key, brush);
    return brush;
}

ID2D1StrokeStyle* Painter::RoundStroke() {
    if (round_stroke_) return round_stroke_;
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return nullptr;
    D2D1_STROKE_STYLE_PROPERTIES props{};
    props.startCap = D2D1_CAP_STYLE_ROUND;
    props.endCap = D2D1_CAP_STYLE_ROUND;
    props.dashCap = D2D1_CAP_STYLE_ROUND;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    props.miterLimit = 10.0f;
    props.dashStyle = D2D1_DASH_STYLE_SOLID;
    factory->CreateStrokeStyle(props, nullptr, 0, &round_stroke_);
    factory->Release();
    return round_stroke_;
}

void Painter::FillRect(const Rect& r, Color color) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    dc_->FillRectangle(ToD2D(r), Brush(color));
}

void Painter::FillRoundedRect(const Rect& r, float radius, Color color) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    dc_->FillRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(r), radius, radius}, Brush(color));
}

void Painter::StrokeRoundedRect(const Rect& r, float radius, Color color, float width) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    // 描边向内收半个线宽做像素对齐，填充不收。
    const Rect inset = r.Inset(width * 0.5f, width * 0.5f);
    radius = std::max(0.0f, radius - width * 0.5f);
    dc_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(inset), radius, radius}, Brush(color),
                              width, RoundStroke());
}

void Painter::DrawLine(Point a, Point b, Color color, float width) {
    if (!dc_ || color.a <= 0.0f) return;
    dc_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), Brush(color), width,
                  RoundStroke());
}

void Painter::PushClip(const Rect& r) {
    if (!dc_) return;
    dc_->PushAxisAlignedClip(ToD2D(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Painter::PopClip() {
    if (!dc_) return;
    dc_->PopAxisAlignedClip();
}

void Painter::DrawLayout(IDWriteTextLayout* layout, const Rect& r, Color color, Align align) {
    if (!layout) return;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float x = r.x;
    if (align == Align::Center) x = r.x + (r.w - metrics.widthIncludingTrailingWhitespace) * 0.5f;
    else if (align == Align::Trailing) x = r.Right() - metrics.widthIncludingTrailingWhitespace;
    float y = r.y + (r.h - metrics.height) * 0.5f;
    x = Snap(x, scale_);
    y = Snap(y, scale_);
    dc_->DrawTextLayout(D2D1::Point2F(x, y), layout, Brush(color), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Painter::DrawText(std::wstring_view text, const Rect& r, TextRole role, Color color,
                       Align align, float max_width) {
    if (!dc_ || !text_ || r.IsEmpty() || text.empty()) return;
    float width = max_width > 0.0f ? max_width : r.w;
    IDWriteTextFormat* format = text_->Format(role);
    if (!format) return;
    // 行盒上下取整到物理像素，保证基线稳定（与 DirectWrite 回退路径一致）。
    Rect snapped = r;
    snapped.y = Snap(r.y, scale_);
    snapped.h = std::max(Snap(r.Bottom(), scale_) - snapped.y, 1.0f / scale_);
    if (luma_) {
        // LumaText 的文字已按物理像素栅格化到命令列表，必须在恒等变换下
        // 1:1 上屏；经 DPI 变换放大就会发虚。字号由 bridge 按 scale 换算。
        const D2D1_RECT_F physical{
            snapped.x * scale_, snapped.y * scale_,
            (snapped.x + width) * scale_, snapped.Bottom() * scale_};
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        const bool drawn = luma_->Draw(text, format, physical, ToD2D(color),
                                       ToD2D(backdrop_), scale_, MapAlign(align));
        dc_->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_));
        if (drawn) return;
    }
    DrawLayout(text_->LineLayout(text, format, width, align), snapped, color, align);
}

Size Painter::MeasureText(std::wstring_view text, TextRole role, float max_width) {
    if (!text_) return {};
    return text_->MeasureText(text, role, max_width);
}

namespace {

bool CanBreakAfter(wchar_t ch) noexcept {
    if (ch == L' ') return true;
    // CJK 统一表意区与全角区允许在字符后断行
    return (ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xFF00 && ch <= 0xFFEF);
}

// 逐字符贪心换行：空格处优先断（行尾空格不保留），CJK 字符后可断，
// 单词超宽时硬切。每字符宽度查询命中 TextService 布局缓存。
std::vector<std::wstring_view> WrapLines(TextService& text, std::wstring_view text_view,
                                         TextRole role, float width) {
    std::vector<std::wstring_view> lines;
    size_t line_start = 0;
    size_t last_break = std::wstring_view::npos;
    float x = 0.0f;
    for (size_t i = 0; i < text_view.size(); ++i) {
        const float char_w = text.MeasureText(text_view.substr(i, 1), role, 0.0f).w;
        if (x + char_w > width && i > line_start) {
            if (last_break != std::wstring_view::npos && last_break > line_start) {
                lines.push_back(text_view.substr(line_start, last_break - line_start));
                line_start = last_break;
            } else {
                lines.push_back(text_view.substr(line_start, i - line_start));
                line_start = i;
            }
            while (line_start < text_view.size() && text_view[line_start] == L' ') ++line_start;
            x = 0.0f;
            last_break = std::wstring_view::npos;
            i = line_start - 1;   // for 循环 ++i 后从新行首重扫
            continue;
        }
        x += char_w;
        if (CanBreakAfter(text_view[i])) {
            last_break = text_view[i] == L' ' ? i : i + 1;
        }
    }
    if (line_start < text_view.size()) lines.push_back(text_view.substr(line_start));
    if (lines.empty()) lines.push_back(text_view);
    return lines;
}

} // namespace

float Painter::DrawTextWrapped(std::wstring_view text, const Rect& r, TextRole role, Color color) {
    if (!dc_ || !text_ || r.IsEmpty() || text.empty()) return 0.0f;
    const float line_h = text_->MeasureText(L"m4B", role, 0.0f).h;   // "测"
    if (!(line_h > 0.0f)) return 0.0f;
    const auto lines = WrapLines(*text_, text, role, r.w);
    float y = r.y;
    for (std::wstring_view line : lines) {
        DrawText(line, {r.x, y, r.w, line_h}, role, color);
        y += line_h;
    }
    return y - r.y;
}

float Painter::MeasureTextWrapped(std::wstring_view text, TextRole role, float wrap_width) {
    return MeasureWrappedHeight(text_ ? *text_ : UiText(), text, role, wrap_width);
}

void Painter::DrawIcon(std::wstring_view glyph, const Rect& r, float size, Color color,
                       Align align) {
    if (!dc_ || !text_ || r.IsEmpty() || glyph.empty()) return;
    IDWriteTextFormat* format = text_->IconFormat(size);
    if (!format) return;
    if (luma_) {
        // 图标字形同样按物理像素 1:1 呈现（级联已含 Segoe MDL2/Fluent Icons）。
        const D2D1_RECT_F physical{r.x * scale_, r.y * scale_, r.Right() * scale_,
                                   r.Bottom() * scale_};
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        const bool drawn =
            luma_->Draw(glyph, format, physical, ToD2D(color), ToD2D(backdrop_), scale_,
                        align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                            : (align == Align::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                        : DWRITE_TEXT_ALIGNMENT_LEADING));
        dc_->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_));
        if (drawn) return;
    }
    IDWriteTextLayout* layout = text_->LineLayout(glyph, format, 1.0e5f, Align::Leading);
    if (!layout) return;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float x = align == Align::Center ? r.x + (r.w - metrics.width) * 0.5f
                                     : (align == Align::Trailing ? r.Right() - metrics.width : r.x);
    float y = r.y + (r.h - metrics.height) * 0.5f;
    dc_->DrawTextLayout(D2D1::Point2F(Snap(x, scale_), Snap(y, scale_)), layout, Brush(color),
                        D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Painter::DrawFocusRing(const Rect& r, float radius, Color accent, float width) {
    if (!dc_ || accent.a <= 0.0f) return;
    const Rect ring = r.Inset(-1.0f, -1.0f);
    StrokeRoundedRect(ring, radius + 1.0f, accent, width);
}

float MeasureWrappedHeight(TextService& text, std::wstring_view text_view, TextRole role,
                           float width) {
    if (text_view.empty() || !(width > 0.0f)) return 0.0f;
    const float line_h = text.MeasureText(L"m4B", role, 0.0f).h;
    const auto lines = WrapLines(text, text_view, role, width);
    return static_cast<float>(lines.size()) * line_h;
}

} // namespace fui

#include "fluentui/Painter.h"
#include "text_service.h"
#include <cmath>

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
    props.startCap = D2D1_CAP_STYLE_FLAT;
    props.endCap = D2D1_CAP_STYLE_FLAT;
    props.dashCap = D2D1_CAP_STYLE_FLAT;
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
    dc_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(r), radius, radius}, Brush(color),
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
    DrawLayout(text_->LineLayout(text, format, width, align), r, color, align);
}

Size Painter::MeasureText(std::wstring_view text, TextRole role, float max_width) {
    if (!text_) return {};
    return text_->MeasureText(text, role, max_width);
}

float Painter::DrawTextWrapped(std::wstring_view text, const Rect& r, TextRole role, Color color) {
    if (!dc_ || !text_ || r.IsEmpty() || text.empty()) return 0.0f;
    IDWriteTextFormat* format = text_->Format(role);
    if (!format) return 0.0f;
    IDWriteTextLayout* layout = text_->WrapLayout(text, format, r.w);
    if (!layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float y = Snap(r.y, scale_);
    float x = Snap(r.x, scale_);
    dc_->DrawTextLayout(D2D1::Point2F(x, y), layout, Brush(color), D2D1_DRAW_TEXT_OPTIONS_NONE);
    return metrics.height;
}

float Painter::MeasureTextWrapped(std::wstring_view text, TextRole role, float wrap_width) {
    if (!text_) return 0.0f;
    return text_->MeasureWrapped(text, role, wrap_width);
}

void Painter::DrawIcon(std::wstring_view glyph, const Rect& r, float size, Color color,
                       Align align) {
    if (!dc_ || !text_ || r.IsEmpty() || glyph.empty()) return;
    IDWriteTextFormat* format = text_->IconFormat(size);
    if (!format) return;
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

void Painter::DrawFocusRing(const Rect& r, float radius, Color color, float inset) {
    if (!dc_ || color.a <= 0.0f) return;
    Rect ring = r.Inset(1.0f + inset, 1.0f + inset);
    StrokeRoundedRect(ring, std::max(1.0f, radius - 1.0f), color, 2.0f);
}

} // namespace fui

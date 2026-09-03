// chart_geom.h — 图表抽样 / 样条 / 悬停。仅控件 .cpp 包含；绘制路径零堆。
#pragma once
#include "lumen/Core.h"
#include "lumen/Painter.h"
#include "lumen/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

namespace lumen::chart_geom {

constexpr size_t kMax = 256;
constexpr size_t kSplineDiv = 8;
constexpr size_t kSplineMax = kMax * kSplineDiv + 1;

inline size_t SampleCount(size_t count, float width_dip) noexcept {
    if (count == 0) return 0;
    size_t by_px = kMax;
    if (width_dip > 2.0f) by_px = static_cast<size_t>(width_dip);
    return std::min(count, std::min(by_px, kMax));
}

inline size_t SourceIndex(size_t sample, size_t samples, size_t count) noexcept {
    if (samples <= 1 || count <= 1) return 0;
    return sample * (count - 1) / (samples - 1);
}

inline size_t WindowIndex(size_t sample, size_t samples, size_t count, float v0,
                          float v1) noexcept {
    if (count <= 1) return 0;
    if (v0 <= 0.0f && v1 >= 1.0f) return SourceIndex(sample, samples, count);
    const float t = samples <= 1 ? 0.5f : static_cast<float>(sample) / static_cast<float>(samples - 1);
    const float u = v0 + Clamp(t, 0.0f, 1.0f) * (v1 - v0);
    const float idx = u * static_cast<float>(count - 1);
    const size_t i = static_cast<size_t>(idx + 0.5f);
    return i >= count ? count - 1 : i;
}

inline size_t CopyEven(float* dst, size_t cap, std::span<const float> src) noexcept {
    if (!dst || cap == 0 || src.empty()) return 0;
    if (src.size() <= cap) {
        std::memcpy(dst, src.data(), src.size() * sizeof(float));
        return src.size();
    }
    for (size_t i = 0; i < cap; ++i) dst[i] = src[SourceIndex(i, cap, src.size())];
    return cap;
}

inline size_t HoverIndex(const Rect& box, float local_x, size_t samples) noexcept {
    if (samples == 0) return 0;
    if (samples == 1) return 0;
    const float t = Clamp((local_x - (box.x)) / std::max(1.0f, box.w), 0.0f, 1.0f);
    return static_cast<size_t>(t * static_cast<float>(samples - 1) + 0.5f);
}

inline void NormalizeRange(float& mn, float& mx) noexcept {
    if (mx - mn < 1.0e-6f) {
        mx = mn + 1.0f;
        mn -= 1.0f;
    }
}

inline void NiceAxis(float& mn, float& mx) noexcept {
    NormalizeRange(mn, mx);
    if (mn >= 0.0f && mx <= 1.001f) return;
    if (mn >= 0.0f && mx <= 100.0f) {
        mn = 0.0f;
        mx = 100.0f;
    }
}

inline int AsPercent(float v) noexcept {
    if (v > 1.001f) return static_cast<int>(v + 0.5f);
    return static_cast<int>(v * 100.0f + 0.5f);
}

inline Point MapX(const Rect& box, size_t i, size_t n, float y, float mn, float mx) noexcept {
    const float t = n <= 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
    const float ny = (y - mn) / std::max(1.0e-6f, mx - mn);
    return {box.x + t * box.w, box.Bottom() - ny * box.h};
}

inline Point CatmullPoint(Point p0, Point p1, Point p2, Point p3, float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return {0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                    (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                    (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
            0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                    (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                    (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3)};
}

inline size_t ExpandSpline(const Point* src, size_t n, Point* out) noexcept {
    if (n == 0) return 0;
    if (n == 1) {
        out[0] = src[0];
        return 1;
    }
    size_t w = 0;
    out[w++] = src[0];
    for (size_t i = 0; i + 1 < n; ++i) {
        const Point p0 = src[i == 0 ? 0 : i - 1];
        const Point p1 = src[i];
        const Point p2 = src[i + 1];
        const Point p3 = src[i + 2 < n ? i + 2 : n - 1];
        for (size_t s = 1; s <= kSplineDiv; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(kSplineDiv);
            if (w < kSplineMax) out[w++] = CatmullPoint(p0, p1, p2, p3, t);
        }
    }
    return w;
}

inline Color Tone(const Theme& theme, float t01) noexcept {
    const float t = Clamp(t01, 0.0f, 1.0f);
    const float lo = 0.42f;
    const float hi = theme.text.r;
    const float v = lo + (hi - lo) * t;
    return {v, v, v, 1.0f};
}

inline Color InkOnTone(const Theme& theme, float t01) noexcept {
    return t01 > 0.48f ? theme.primary_text : theme.text;
}

inline Color Heat(const Theme& theme, float t01) noexcept {
    const float t = Clamp(t01, 0.0f, 1.0f);
    const float v = 0.12f + (theme.text.r - 0.12f) * t;
    return {v, v, v, 1.0f};
}

inline void FormatValue(float v, wchar_t* buf, int cap) noexcept {
    if (!buf || cap < 2) return;
    const float av = std::fabs(v);
    if (av >= 10000.0f) {
        std::swprintf(buf, static_cast<size_t>(cap), L"%.0f", v);
    } else if (av >= 100.0f || std::fabs(v - std::floor(v + 0.5f)) < 0.05f) {
        std::swprintf(buf, static_cast<size_t>(cap), L"%.0f", v);
    } else {
        std::swprintf(buf, static_cast<size_t>(cap), L"%.1f", v);
    }
}

inline void DrawGrid(Painter& painter, const Theme& theme, const Rect& box) {
    for (int i = 0; i <= 2; ++i) {
        const float y = box.y + box.h * (static_cast<float>(i) / 2.0f);
        painter.DrawLine({box.x, y}, {box.Right(), y}, theme.stroke_divider, 1.0f);
    }
}

inline void DrawCrosshair(Painter& painter, const Theme& theme, const Rect& box, Point p) {
    const Color ink = theme.stroke_card;
    painter.DrawLine({p.x, box.y}, {p.x, box.Bottom()}, ink, 1.0f);
    painter.DrawLine({box.x, p.y}, {box.Right(), p.y}, ink, 1.0f);
}

inline void DrawHover(Painter& painter, const Theme& theme, const Rect& box, Point p, float value) {
    painter.DrawLine({p.x, box.y}, {p.x, box.Bottom()}, theme.stroke_divider, 1.0f);
    painter.FillRoundedRect({p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f}, 3.0f, theme.text);
    wchar_t buf[24];
    FormatValue(value, buf, 24);
    const float tw = painter.MeasureText(buf, TextRole::Mono).w + 12.0f;
    float lx = p.x + 8.0f;
    if (lx + tw > box.Right()) lx = p.x - tw - 8.0f;
    const Rect tag{lx, std::max(box.y, p.y - 22.0f), tw, 18.0f};
    painter.FillRoundedRect(tag, 4.0f, theme.fill_input);
    painter.StrokeRoundedRect(tag, 4.0f, theme.stroke_card, 1.0f);
    painter.DrawText(buf, tag, TextRole::Mono, theme.text, Align::Center);
}

inline void FillDot(Painter& painter, Point c, float r, Color color) {
    painter.FillRoundedRect({c.x - r, c.y - r, r * 2.0f, r * 2.0f}, r, color);
}

inline void StrokeDot(Painter& painter, Point c, float r, Color color, float width) {
    painter.StrokeRoundedRect({c.x - r, c.y - r, r * 2.0f, r * 2.0f}, r, color, width);
}

inline void DrawDashGrid(Painter& painter, const Theme& theme, const Rect& box, float mn,
                         float mx) {
    for (int i = 0; i <= 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float y = box.Bottom() - t * box.h;
        painter.DrawDashedLine({box.x, y}, {box.Right(), y}, theme.stroke_card, 1.0f);
        wchar_t buf[16];
        FormatValue(mn + t * (mx - mn), buf, 16);
        painter.DrawText(buf, {box.x - 38.0f, y - 8.0f, 34.0f, 16.0f}, TextRole::Caption,
                         theme.text_secondary, Align::Trailing, 34.0f);
    }
}

inline void DrawCallout(Painter& painter, const Theme& theme, Point anchor, const Rect& clip,
                        std::wstring_view title, std::wstring_view l0, std::wstring_view v0,
                        Color d0, std::wstring_view l1 = {}, std::wstring_view v1 = {},
                        Color d1 = {}) {
    const int rows = l1.empty() ? 1 : 2;
    const float pad = 10.0f;
    const float row_h = 18.0f;
    const float header_h = 24.0f;
    const float w = 132.0f;
    const float h = header_h + 6.0f + row_h * static_cast<float>(rows) + pad;
    float x = anchor.x + 14.0f;
    float y = anchor.y - h - 8.0f;
    if (x + w > clip.Right() - 2.0f) x = anchor.x - w - 14.0f;
    if (x < clip.x + 2.0f) x = clip.x + 2.0f;
    if (y < clip.y) y = anchor.y + 8.0f;
    if (y + h > clip.Bottom()) y = clip.Bottom() - h;
    if (y < clip.y) y = clip.y;
    const Rect card{x, y, w, h};
    painter.FillRoundedRect(card, 8.0f, theme.surface_flyout);
    painter.StrokeRoundedRect(card, 8.0f, theme.stroke_card, 1.0f);
    painter.DrawText(title, {x + pad, y + 5.0f, w - pad * 2.0f, 18.0f}, TextRole::BodyStrong,
                     theme.text, Align::Leading, w - pad * 2.0f);
    painter.DrawLine({x + pad, y + header_h}, {x + w - pad, y + header_h}, theme.stroke_divider,
                     1.0f);
    float ry = y + header_h + 5.0f;
    auto row = [&](std::wstring_view lab, std::wstring_view val, Color dot) {
        FillDot(painter, {x + pad + 4.0f, ry + 9.0f}, 3.0f, dot);
        painter.DrawText(lab, {x + pad + 14.0f, ry, 64.0f, row_h}, TextRole::Caption,
                         theme.text_secondary, Align::Leading, 64.0f);
        painter.DrawText(val, {x + w - pad - 44.0f, ry, 44.0f, row_h}, TextRole::BodyStrong,
                         theme.text, Align::Trailing, 44.0f);
        ry += row_h;
    };
    row(l0, v0, d0);
    if (rows == 2) row(l1, v1, d1.a > 0.0f ? d1 : theme.text_secondary);
}

}  // namespace lumen::chart_geom

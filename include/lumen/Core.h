// lumen/Core.h — 基础几何与颜色类型。公共 API 不暴露 D2D 类型，转换为库内部完成。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <cstdint>
#include <cmath>

namespace lumen {

struct Point {
    float x = 0.0f, y = 0.0f;
};

struct Size {
    float w = 0.0f, h = 0.0f;
};

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    float Right() const noexcept { return x + w; }
    float Bottom() const noexcept { return y + h; }
    bool IsEmpty() const noexcept { return w <= 0.0f || h <= 0.0f; }
    bool Contains(Point p) const noexcept {
        return p.x >= x && p.x < Right() && p.y >= y && p.y < Bottom();
    }
    bool Contains(float px, float py) const noexcept { return Contains(Point{px, py}); }
    Rect Inset(float dx, float dy) const noexcept { return {x + dx, y + dy, w - dx * 2.0f, h - dy * 2.0f}; }
    Rect Offset(float dx, float dy) const noexcept { return {x + dx, y + dy, w, h}; }
    Rect Intersect(const Rect& o) const noexcept {
        const float nx = x > o.x ? x : o.x;
        const float ny = y > o.y ? y : o.y;
        const float nr = Right() < o.Right() ? Right() : o.Right();
        const float nb = Bottom() < o.Bottom() ? Bottom() : o.Bottom();
        return {nx, ny, nr > nx ? nr - nx : 0.0f, nb > ny ? nb - ny : 0.0f};
    }
};

inline Rect UnionRect(const Rect& a, const Rect& b) noexcept {
    if (a.IsEmpty()) return b;
    if (b.IsEmpty()) return a;
    const float x = a.x < b.x ? a.x : b.x;
    const float y = a.y < b.y ? a.y : b.y;
    const float r = a.Right() > b.Right() ? a.Right() : b.Right();
    const float bot = a.Bottom() > b.Bottom() ? a.Bottom() : b.Bottom();
    return {x, y, r - x, bot - y};
}

// 控件失效外扩：按钮辉光 / 焦点环伸出 absolute_；聚光卡用整卡，不走此值。
inline constexpr float kDirtyPadDip = 32.0f;
inline constexpr int kMaxDirtyRects = 8;

struct Thickness {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

    static Thickness Uniform(float v) noexcept { return {v, v, v, v}; }
    static Thickness HV(float horizontal, float vertical) noexcept {
        return {horizontal, vertical, horizontal, vertical};
    }
    float Horizontal() const noexcept { return left + right; }
    float Vertical() const noexcept { return top + bottom; }
};

struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    static constexpr Color Hex(uint32_t rgb, float alpha = 1.0f) noexcept {
        return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
    }
    // 0xAARRGGBB
    static constexpr Color ARGB(uint32_t argb) noexcept {
        return Hex(argb & 0xFFFFFFu, static_cast<float>((argb >> 24) & 0xFF) / 255.0f);
    }
};

enum class Align { Leading, Center, Trailing };
enum class CrossAlign { Stretch, Start, Center, End };
enum class MainAlign { Start, Center, End, SpaceBetween };
// Subtitle 补 Title/Body 之间的档；Overline 为加字距小标题；Numeric 开表格数字。
enum class TextRole {
    Body,
    BodyStrong,
    Caption,
    CaptionStrong,
    Title,
    Icon,
    Mono,
    Display,
    Subtitle,
    Overline,
    Numeric
};
inline constexpr size_t kTextRoleCount = 11;
enum class SliderOrientation { Horizontal, Vertical };

inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

template <typename T>
constexpr T Clamp(T v, T lo, T hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

// 覆盖在视口右/下沿的圆头滑块（ListView / ScrollViewer 共用）。expand 0..1 控制 2.5→5px。
struct ScrollThumb {
    Rect rect{};
    bool visible = false;
};

inline ScrollThumb MakeScrollThumb(const Rect& viewport, float content, float offset, float expand,
                                   bool vertical) noexcept {
    const float view = vertical ? viewport.h : viewport.w;
    if (!(content > view + 0.5f) || view < 1.0f) return {};
    const float t = Clamp(expand, 0.0f, 1.0f);
    const float thickness = 2.5f + 2.5f * t;
    float thumb_extent = view * view / content;
    if (thumb_extent < 20.0f) thumb_extent = 20.0f;
    if (thumb_extent > view) thumb_extent = view;
    const float track = view - thumb_extent;
    const float range = content - view;
    const float pos = range > 0.0f ? offset / range * track : 0.0f;
    ScrollThumb thumb;
    thumb.visible = true;
    if (vertical) {
        thumb.rect = {viewport.Right() - 2.5f - thickness, viewport.y + pos, thickness,
                      thumb_extent};
    } else {
        thumb.rect = {viewport.x + pos, viewport.Bottom() - 2.5f - thickness, thumb_extent,
                      thickness};
    }
    return thumb;
}

} // namespace lumen

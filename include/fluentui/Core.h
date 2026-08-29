// fluentui/Core.h — 基础几何与颜色类型。公共 API 不暴露 D2D 类型，转换为库内部完成。
#pragma once
#include <cstdint>
#include <cmath>

namespace fui {

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
enum class TextRole { Body, BodyStrong, Caption, CaptionStrong, Title, Icon, Mono };

inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

template <typename T>
constexpr T Clamp(T v, T lo, T hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace fui

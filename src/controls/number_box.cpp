#include "lumen/NumberBox.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

namespace lumen {

NumberBox& NumberBox::Range(double min_value, double max_value) {
    min_ = std::min(min_value, max_value);
    max_ = max_value;
    if (!text_.empty()) {
        const double current = Value();
        Commit(current, false);
    }
    return *this;
}

NumberBox& NumberBox::Value(double value) {
    Commit(Clamp(value, min_, max_), false);
    return *this;
}

bool NumberBox::TryParse(double& out) const {
    if (text_.empty()) return false;
    wchar_t* end = nullptr;
    const double parsed = wcstod(text_.c_str(), &end);
    if (end == text_.c_str()) return false;
    while (end && *end) {
        if (!std::iswspace(static_cast<wint_t>(*end))) return false;
        ++end;
    }
    out = parsed;
    return true;
}

double NumberBox::Value() const {
    double parsed = 0.0;
    return TryParse(parsed) ? Clamp(parsed, min_, max_) : min_;
}

std::wstring NumberBox::Format(double value) const {
    wchar_t buffer[48]{};
    const int precision = decimals_;
    // 负零与 -0.0 的显示钳到 0。
    if (std::fabs(value) < 0.5 * std::pow(10.0, -(precision + 1))) value = 0.0;
    if (precision > 0) {
        swprintf(buffer, 48, L"%.*f", precision, value);
    } else {
        swprintf(buffer, 48, L"%.0f", value);
    }
    return buffer;
}

void NumberBox::Commit(double value, bool notify) {
    value = Clamp(value, min_, max_);
    Text(Format(value));   // 不入撤销栈、不触发 OnTextChanged
    if (notify) changed_.Emit(value);
    Invalidate();
}

void NumberBox::StepSpin(int zone) {
    double current = 0.0;
    if (!TryParse(current)) current = min_;
    Commit(current + (zone == 0 ? step_ : -step_), true);
}

float NumberBox::PadRight() const {
    return spin_ ? TextBox::PadRight() + kSpinWidth : TextBox::PadRight();
}

bool NumberBox::OnChar(wchar_t ch) {
    if (read_only_) return false;
    // 只放行数字语法字符；负号只允许出现在开头（交给失焦钳制兜底）。
    const bool digit = ch >= L'0' && ch <= L'9';
    const bool sign = ch == L'-' || ch == L'+';
    const bool dot = ch == L'.';
    const bool exponent = ch == L'e' || ch == L'E';
    if (!digit && !sign && !dot && !exponent) return true;   // 吞掉但不交给 IME 链
    return TextBox::OnChar(ch);
}

int NumberBox::SpinZoneAt(Point local) const {
    if (!spin_ || local.x < absolute_.w - kSpinWidth || local.x > absolute_.w) return -1;
    if (local.y < 0.0f || local.y > absolute_.h) return -1;
    return local.y < absolute_.h * 0.5f ? 0 : 1;
}

bool NumberBox::OnKey(uint32_t vk) {
    if (read_only_) return TextBox::OnKey(vk);
    if (vk == VK_UP || vk == VK_DOWN) {
        double current = 0.0;
        if (!TryParse(current)) current = min_;
        Commit(current + (vk == VK_UP ? step_ : -step_), true);
        return true;
    }
    return TextBox::OnKey(vk);
}

void NumberBox::OnFocusChanged(bool focused) {
    if (!focused && !text_.empty()) {
        // 失焦兜底：解析失败回 min，越界钳制，统一格式化。
        double parsed = 0.0;
        Commit(TryParse(parsed) ? parsed : min_, true);
    }
    TextBox::OnFocusChanged(focused);
}

void NumberBox::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & MK_LBUTTON)) return;
    const int zone = SpinZoneAt(local);
    if (spin_ && zone >= 0 && !read_only_) {
        Focus();
        mouse_local_ = local;
        spin_pressed_ = zone;
        StepSpin(zone);
        spin_hold_.Press();
        Animate();
        Invalidate();
        return;
    }
    TextBox::OnMouseDown(local, buttons);
}

void NumberBox::OnMouseMove(Point local, uint32_t buttons) {
    if (spin_pressed_ >= 0) {
        mouse_local_ = local;
        return;
    }
    TextBox::OnMouseMove(local, buttons);
}

void NumberBox::OnMouseUp(Point local, uint32_t buttons) {
    if (spin_pressed_ >= 0) {
        spin_pressed_ = -1;
        spin_hold_.Release();
        Invalidate();
        return;
    }
    TextBox::OnMouseUp(local, buttons);
}

bool NumberBox::OnAnimate(float dt_seconds) {
    bool moving = TextBox::OnAnimate(dt_seconds);
    if (spin_pressed_ < 0 || !spin_hold_.armed) return moving;
    if (!enabled_ || read_only_) {
        spin_pressed_ = -1;
        spin_hold_.Release();
        return moving;
    }
    const bool active = enabled_ && !read_only_ && SpinZoneAt(mouse_local_) == spin_pressed_;
    const int n = spin_hold_.Tick(dt_seconds, active);
    for (int i = 0; i < n; ++i) StepSpin(spin_pressed_);
    return true;
}

CursorShape NumberBox::CursorAt(Point local) const {
    return SpinZoneAt(local) >= 0 ? CursorShape::Arrow : TextBox::CursorAt(local);
}

void NumberBox::Draw(Painter& painter, const Theme& theme) {
    TextBox::Draw(painter, theme);
    if (!spin_ || absolute_.IsEmpty()) return;

    // 命中区为完整上下半格；绘制在四周留呼吸边距，避免箭头贴上下右三边。
    const Rect upper{absolute_.Right() - kSpinWidth, absolute_.y, kSpinWidth, absolute_.h * 0.5f};
    const Rect lower{absolute_.Right() - kSpinWidth, absolute_.y + absolute_.h * 0.5f, kSpinWidth,
                     absolute_.h * 0.5f};
    const bool active = enabled_ && !read_only_;
    Color glyph = active ? theme.text_secondary : theme.text_disabled;
    if (active && spin_pressed_ >= 0) glyph = theme.text;
    // Fluent NumberBox 惯例：文本区与 spin 区之间一条细分隔线。
    painter.FillRect({upper.x, absolute_.y + 5.0f, 1.0f, absolute_.h - 10.0f},
                     theme.stroke_divider);
    painter.DrawIcon(icon::kChevronUp, {upper.x, upper.y + 2.0f, kSpinWidth, upper.h - 3.0f}, 9.0f,
                     glyph);
    painter.DrawIcon(icon::kChevronDown, {lower.x, lower.y + 1.0f, kSpinWidth, lower.h - 3.0f},
                     9.0f, glyph);
    if (spin_pressed_ == 0) {
        painter.FillRoundedRect({upper.x + 2.0f, upper.y + 1.0f, kSpinWidth - 4.0f, upper.h - 2.0f},
                                4.0f, theme.fill_hover);
    } else if (spin_pressed_ == 1) {
        painter.FillRoundedRect({lower.x + 2.0f, lower.y + 1.0f, kSpinWidth - 4.0f, lower.h - 2.0f},
                                4.0f, theme.fill_hover);
    }
}

NumberBox& NumberBox::BindValue(Property<float>& p, float scale) {
    if (scale == 0.0f) scale = 1.0f;
    auto apply = [this, &p, scale] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Value(static_cast<double>(p.Get() * scale));
        bind_loop_ = false;
    };
    apply();
    value_prop_ = ScopedConnection(p.OnChanged([apply](const float&) { apply(); }));
    value_ctrl_ = ScopedConnection(changed_.Connect([this, &p, scale](double v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = static_cast<float>(v / static_cast<double>(scale));
        bind_loop_ = false;
    }));
    return *this;
}

NumberBox& NumberBox::BindValue(Property<double>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Value(p.Get());
        bind_loop_ = false;
    };
    apply();
    value_prop_ = ScopedConnection(p.OnChanged([apply](const double&) { apply(); }));
    value_ctrl_ = ScopedConnection(changed_.Connect([this, &p](double v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = v;
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen

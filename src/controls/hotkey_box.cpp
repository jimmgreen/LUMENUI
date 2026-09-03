#include "lumen/HotkeyBox.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/hotkey.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace lumen {
namespace {
constexpr float kPadX = 10.0f;
constexpr float kClear = 28.0f;

bool IsModifier(uint32_t vk) noexcept {
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool NeedsModifier(uint32_t vk) noexcept {
    if (vk >= 'A' && vk <= 'Z') return true;
    if (vk >= '0' && vk <= '9') return true;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return true;
    switch (vk) {
    case VK_OEM_1:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_OEM_8:
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_SPACE:
    case VK_RETURN:
        return true;
    default:
        return false;
    }
}

} // namespace

void HotkeyBox::RelayoutParent() { Control::RelayoutParent(); }

bool HotkeyBox::Assign(uint32_t vk, bool ctrl, bool shift, bool alt, bool notify) {
    if (vk_ == vk && ctrl_ == ctrl && shift_ == shift && alt_ == alt) return false;
    vk_ = vk;
    ctrl_ = ctrl;
    shift_ = shift;
    alt_ = alt;
    chord_ = FormatChord(vk_, ctrl_, shift_, alt_);
    Invalidate();
    if (notify) changed_.Emit();
    return true;
}

HotkeyBox& HotkeyBox::Chord(std::wstring_view value) {
    uint32_t vk = 0;
    bool ctrl = false, shift = false, alt = false;
    if (!ParseChord(value, vk, ctrl, shift, alt)) return *this;
    Assign(vk, ctrl, shift, alt, false);
    return *this;
}

HotkeyBox& HotkeyBox::Clear() {
    Assign(0, false, false, false, true);
    return *this;
}

Rect HotkeyBox::ClearRect() const noexcept {
    if (vk_ == 0) return {};
    return {absolute_.Right() - kClear, absolute_.y, kClear, absolute_.h};
}

bool HotkeyBox::InClear(Point local) const noexcept {
    if (vk_ == 0) return false;
    return local.x >= absolute_.w - kClear;
}

Size HotkeyBox::Measure(Size available, const Theme& theme) {
    const float width = (available.w >= 0.0f && available.w < 1.0e4f) ? available.w : 180.0f;
    return {width, theme.input_height};
}

void HotkeyBox::Draw(Painter& painter, const Theme& theme) {
    const float radius = theme.radius_control;
    Color fill = theme.fill_input;
    Color stroke = theme.control_stroke;
    if (!enabled_) {
        fill = theme.fill_input_disabled;
    } else if (focused_) {
        fill = theme.fill_input_focus;
        stroke = theme.accent;
        painter.DrawGlow(absolute_, radius, theme.glow_sm);
    } else if (hovered_) {
        fill = theme.fill_input_hover;
    }
    painter.FillRoundedRect(absolute_, radius, fill);
    if (enabled_) {
        painter.DrawInnerLight(absolute_, radius, theme.edge_light, Color{0.0f, 0.0f, 0.0f, 0.35f});
    }
    painter.StrokeRoundedRect(absolute_, radius, stroke);

    const float clear = vk_ == 0 ? 0.0f : kClear;
    const Rect text_r{absolute_.x + kPadX, absolute_.y, std::max(0.0f, absolute_.w - kPadX - clear),
                      absolute_.h};
    if (vk_ == 0) {
        painter.DrawText(placeholder_, text_r, TextRole::Body, theme.text_secondary);
    } else {
        const Color fg = enabled_ ? theme.text : theme.text_disabled;
        painter.DrawText(chord_, text_r, TextRole::Body, fg, Align::Leading, text_r.w);
        const Rect close = ClearRect();
        painter.DrawIcon(icon::kClose, close, 12.0f,
                         hovered_ && InClear({mouse_local_.x, mouse_local_.y}) ? theme.text
                                                                              : theme.text_secondary);
    }
}

void HotkeyBox::OnFocusChanged(bool) { Invalidate(); }

bool HotkeyBox::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_TAB) return false;
    if (IsModifier(vk)) return true;
    if (vk == VK_BACK || vk == VK_DELETE) {
        if (vk_ != 0) Assign(0, false, false, false, true);
        return true;
    }
    if (vk == VK_ESCAPE) {
        if (vk_ == 0) return false;
        Assign(0, false, false, false, true);
        return true;
    }
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (NeedsModifier(vk) && !ctrl && !alt) return true;
    Assign(vk, ctrl, shift, alt, true);
    return true;
}

bool HotkeyBox::OnChar(wchar_t) { return true; }

void HotkeyBox::OnMouseEnter() {
    Control::OnMouseEnter();
    Invalidate();
}

void HotkeyBox::OnMouseLeave() {
    Control::OnMouseLeave();
    Invalidate();
}

void HotkeyBox::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001) || !enabled_) return;
    Focus();
    if (InClear(local)) Assign(0, false, false, false, true);
}

CursorShape HotkeyBox::CursorAt(Point local) const {
    return InClear(local) ? CursorShape::Hand : CursorShape::Arrow;
}

} // namespace lumen

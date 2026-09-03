#include "lumen/HyperlinkButton.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>

namespace lumen {

void HyperlinkButton::RelayoutParent() { Control::RelayoutParent(); }

Size HyperlinkButton::Measure(Size, const Theme&) {
    const Size size = MeasureText(text_, TextRole::Body);
    return {std::max(size.w, 1.0f), std::max(size.h, 20.0f)};
}

void HyperlinkButton::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

CursorShape HyperlinkButton::CursorAt(Point) const { return CursorShape::Hand; }

bool HyperlinkButton::OnKey(uint32_t vk) {
    if (vk == VK_RETURN || vk == VK_SPACE) {
        if (enabled_) click_.Emit();
        return true;
    }
    return false;
}

void HyperlinkButton::OnMouseUp(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001) || !enabled_) return;
    if (local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w && local.y <= absolute_.h) {
        click_.Emit();
    }
}

void HyperlinkButton::Draw(Painter& painter, const Theme& theme) {
    Color color = theme.text_secondary;
    if (!enabled_) color = theme.text_disabled;
    else if (pressed_) color = theme.text;
    else if (hovered_ || focused_) color = theme.text;
    const Size text = painter.MeasureText(text_, TextRole::Body);
    float w = text.w;
    if (w > absolute_.w) w = absolute_.w;
    const Rect ink{absolute_.x, absolute_.y, w, absolute_.h};
    painter.DrawText(text_, ink, TextRole::Body, color);
    const float y = ink.Bottom() - 3.0f;
    painter.DrawLine({ink.x, y}, {ink.Right(), y}, color);
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, ink, 4.0f);
    }
}

} // namespace lumen

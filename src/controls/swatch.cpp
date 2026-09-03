#include "lumen/Swatch.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"

namespace lumen {

void ColorSwatch::RelayoutParent() { Control::RelayoutParent(); }

Size ColorSwatch::Measure(Size, const Theme&) {
    return {18.0f, 18.0f};
}

void ColorSwatch::Draw(Painter& painter, const Theme& theme) {
    const Rect ring = absolute_.Inset(-3.0f, -3.0f);
    if (selected_) {
        // 选中：白色辉光环 + 外扩
        painter.DrawGlow(ring, 12.0f, theme.glow_sm);
        painter.StrokeRoundedRect(ring, 12.0f, theme.accent, 2.0f);
    } else if (hovered_) {
        const lumen::Color hover_ring{theme.text.r, theme.text.g, theme.text.b, 0.35f};
        const Rect hover_box = absolute_.Inset(-2.0f, -2.0f);
        painter.StrokeRoundedRect(hover_box, 11.0f, hover_ring, 1.0f);
    }
    painter.FillRoundedRect(absolute_, 9.0f, color_);
}

void ColorSwatch::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (local.x >= -4.0f && local.y >= -4.0f && local.x <= absolute_.w + 4.0f &&
        local.y <= absolute_.h + 4.0f) {
        picked_.Emit();
    }
}

bool ColorSwatch::OnKey(uint32_t vk) {
    if (vk == VK_SPACE || vk == VK_RETURN) {
        picked_.Emit();
        return true;
    }
    return false;
}

} // namespace lumen

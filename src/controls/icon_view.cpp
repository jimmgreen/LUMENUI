#include "lumen/IconView.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"

namespace lumen {

void IconView::RelayoutParent() { Control::RelayoutParent(); }

Size IconView::Measure(Size, const Theme&) {
    return {box_, box_};
}

void IconView::Draw(Painter& painter, const Theme& theme) {
    const Color fg = foreground_.a > 0.0f ? foreground_ : theme.text;
    const Color bg = custom_background_ ? background_ : theme.fill_hover;
    const Color stroke = custom_stroke_ ? stroke_ : theme.control_stroke;
    if (bg.a > 0.0f) painter.FillRoundedRect(absolute_, radius_, bg);
    if (stroke.a > 0.0f) painter.StrokeRoundedRect(absolute_, radius_, stroke);
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, absolute_, icon_size_, fg, Align::Center, weight_);
    }
    if (!badge_.Empty()) {
        PaintInfoBadge(painter, theme, InfoBadgeCorner(absolute_), badge_);
    }
}

} // namespace lumen

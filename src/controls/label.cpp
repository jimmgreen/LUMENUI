#include "lumen/Label.h"
#include "lumen/Painter.h"
#include <algorithm>

namespace lumen {

Size Label::Measure(Size available, const Theme&) {
    if (text_.empty()) {
        if (role_ == TextRole::Display) return {0.0f, 56.0f};
        if (role_ == TextRole::Title) return {0.0f, 28.0f};
        if (role_ == TextRole::Subtitle) return {0.0f, 24.0f};
        if (role_ == TextRole::Overline) return {0.0f, 16.0f};
        return {0.0f, 20.0f};
    }
    if (wrap_) {
        const float width = available.w > 0.0f ? available.w : 400.0f;
        return {width, MeasureWrapped(text_, role_, width)};
    }
    Size size = MeasureText(text_, role_, available.w > 0.0f ? available.w : 1.0e5f);
    if (glow_) size.w += 2.0f;
    return {std::max(size.w, 1.0f), size.h};
}

void Label::Draw(Painter& painter, const Theme& theme) {
    if (text_.empty()) return;
    Color color = theme.text;
    if (!enabled_) color = theme.text_disabled;
    else if (foreground_.a > 0.0f) color = foreground_;
    else if (secondary_ || role_ == TextRole::Overline) color = theme.text_secondary;
    if (wrap_) {
        painter.DrawTextWrapped(text_, absolute_, role_, color, align_);
        return;
    }
    if (glow_ && enabled_) {
        painter.DrawTextGlow(text_, absolute_, role_, color, align_);
        return;
    }
    painter.DrawText(text_, absolute_, role_, color, align_);
}

} // namespace lumen

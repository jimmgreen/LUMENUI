#include "lumen/GroupBox.h"
#include "lumen/Painter.h"
#include <algorithm>

namespace lumen {
namespace {
constexpr float kTitleH = 20.0f;
constexpr float kPad = 12.0f;
constexpr float kGap = 8.0f;
constexpr float kLegendX = 12.0f;
constexpr float kLegendGap = 6.0f;
} // namespace

GroupBox::GroupBox() {
    Spotlight(true);
    spotlight_t_ = 0.0f;
    spotlight_inside_ = false;
}

GroupBox::GroupBox(std::wstring_view title) : title_(title) {
    Spotlight(true);
    spotlight_t_ = 0.0f;
    spotlight_inside_ = false;
}

void GroupBox::RelayoutParent() { Control::RelayoutParent(); }

Size GroupBox::Measure(Size available, const Theme& theme) {
    const float width = (available.w >= 0.0f && available.w < 1.0e4f) ? available.w : 280.0f;
    top_ = title_.empty() ? kPad : kTitleH * 0.5f + kPad;
    const float inner = std::max(0.0f, width - kPad * 2.0f);
    float body = 0.0f;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {inner, 1.0e5f}, theme);
        body += desired.h + (first ? 0.0f : kGap);
        first = false;
    }
    return {width, top_ + body + kPad};
}

void GroupBox::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    float y = top_;
    bool first = true;
    const float inner = std::max(0.0f, absolute.w - kPad * 2.0f);
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        if (!first) y += kGap;
        first = false;
        const Size desired = ChildDesired(i);
        SetChildBounds(Child(i), {kPad, y, inner, desired.h});
        ArrangeChildAt(i);
        y += desired.h;
    }
}

void GroupBox::Draw(Painter& painter, const Theme& theme) {
    const float radius = theme.radius_control;
    Rect frame = absolute_;
    if (!title_.empty()) {
        const float mid = kTitleH * 0.5f;
        frame = {absolute_.x, absolute_.y + mid, absolute_.w, std::max(0.0f, absolute_.h - mid)};
    }
    painter.FillRoundedRect(frame, radius, theme.fill_input);
    DrawSpotlight(painter, theme, frame, radius, SpotlightCenter(), spotlight_t_);
    AvoidControls(painter, theme);
    if (title_.empty()) return;
    const float max_w = std::max(0.0f, absolute_.w - kLegendX * 2.0f);
    const Size ts = MeasureText(title_, TextRole::CaptionStrong, max_w);
    const float tx = absolute_.x + kLegendX;
    painter.FillRect({tx - kLegendGap, frame.y - 2.0f, ts.w + kLegendGap * 2.0f, 5.0f},
                     theme.fill_input);
    const Color fg = enabled_ ? theme.text : theme.text_disabled;
    painter.DrawText(title_, {tx, absolute_.y, ts.w, kTitleH}, TextRole::CaptionStrong, fg,
                     Align::Leading, max_w);
}

} // namespace lumen

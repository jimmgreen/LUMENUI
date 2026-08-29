#include "fluentui/Dialog.h"
#include "fluentui/Painter.h"
#include "fluentui/Window.h"
#include "../core/text_service.h"
#include <algorithm>

namespace fui {
namespace {
constexpr float kCardPad = 24.0f;
constexpr float kButtonH = 32.0f;

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}

float ButtonWidth(const std::wstring& label) {
    return std::clamp(UiText().MeasureText(label, TextRole::Body).w + 32.0f, 96.0f, 180.0f);
}
} // namespace

Dialog& Dialog::PrimaryButton(std::wstring_view label, std::function<void()> action) {
    primary_label_ = std::wstring(label);
    primary_action_ = std::move(action);
    default_close_ = false;
    RelayoutParent();
    return *this;
}

Dialog& Dialog::SecondaryButton(std::wstring_view label, std::function<void()> action) {
    secondary_label_ = std::wstring(label);
    secondary_action_ = std::move(action);
    default_close_ = false;
    RelayoutParent();
    return *this;
}

Dialog& Dialog::DefaultClose() {
    default_close_ = true;
    primary_label_.clear();
    secondary_label_.clear();
    RelayoutParent();
    return *this;
}

void Dialog::Close() {
    if (window_) {
        window_->CloseDialog();
    }
}

Size Dialog::Measure(Size available, const Theme&) {
    const float width = std::min(420.0f, std::max(available.w - 24.0f, 240.0f));
    float height = 24.0f + 28.0f;
    if (!message_.empty()) {
        height += 8.0f + UiText().MeasureWrapped(message_, TextRole::Body, width - kCardPad * 2.0f);
    }
    if (!default_close_ && (!primary_label_.empty() || !secondary_label_.empty())) {
        height += 24.0f + kButtonH;
    }
    height += 24.0f;
    desired_ = {width, height};
    return desired_;
}

void Dialog::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    // 按钮矩形（卡片坐标，右下排列）
    primary_rect_ = {};
    secondary_rect_ = {};
    float x_right = absolute.Right() - kCardPad;
    const float y = absolute.Bottom() - 24.0f - kButtonH;
    if (!primary_label_.empty()) {
        const float w = ButtonWidth(primary_label_);
        primary_rect_ = {x_right - w, y, w, kButtonH};
        x_right -= w + 8.0f;
    }
    if (!secondary_label_.empty()) {
        const float w = ButtonWidth(secondary_label_);
        secondary_rect_ = {x_right - w, y, w, kButtonH};
    }
    // 子控件按相对 bounds_ 排布
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        ArrangeChildAt(i);
    }
}

void Dialog::Draw(Painter& painter, const Theme& theme) {
    painter.FillRoundedRect(absolute_, theme.radius_card, theme.card);
    painter.StrokeRoundedRect(absolute_, theme.radius_card, theme.control_stroke);
    painter.DrawText(title_, {absolute_.x + kCardPad, absolute_.y + 20.0f, absolute_.w - kCardPad * 2.0f, 28.0f},
                     TextRole::Title, theme.text);
    if (!message_.empty()) {
        Rect message{absolute_.x + kCardPad, absolute_.y + 56.0f, absolute_.w - kCardPad * 2.0f,
                     absolute_.Bottom() - 24.0f - kButtonH - 24.0f - (absolute_.y + 56.0f)};
        painter.DrawTextWrapped(message_, message, TextRole::Body, theme.text_secondary);
    }

    auto draw_button = [&](const Rect& rect, const std::wstring& label, bool accent, bool hot,
                           bool press) {
        Color fill = accent ? Mix(theme.accent, theme.accent_hover, hot ? 1.0f : 0.0f)
                            : Mix(theme.control_fill, theme.control_fill_hover, hot ? 1.0f : 0.0f);
        fill = Mix(fill, accent ? theme.accent_pressed : theme.control_fill_pressed,
                   press ? 1.0f : 0.0f);
        if (accent) {
            painter.FillRoundedRect(rect, theme.radius_control, fill);
        } else {
            painter.FillRoundedRect(rect, theme.radius_control, fill);
            painter.StrokeRoundedRect(rect, theme.radius_control, theme.control_stroke);
        }
        painter.DrawText(label, rect, TextRole::Body,
                         accent ? theme.accent_text : theme.text, Align::Center);
    };
    if (!secondary_label_.empty()) {
        draw_button(secondary_rect_, secondary_label_, false, secondary_hot_, secondary_press_);
    }
    if (!primary_label_.empty()) {
        draw_button(primary_rect_, primary_label_, true, primary_hot_, primary_press_);
    }
}

namespace {
bool InRect(const Rect& r, Point local_card, const Rect& absolute) {
    const Point screen{local_card.x + absolute.x, local_card.y + absolute.y};
    return r.Contains(screen);
}
} // namespace

void Dialog::UpdateHot(Point local) {
    const bool ph = !primary_label_.empty() && InRect(primary_rect_, local, absolute_);
    const bool sh = !secondary_label_.empty() && InRect(secondary_rect_, local, absolute_);
    if (ph != primary_hot_ || sh != secondary_hot_) {
        primary_hot_ = ph;
        secondary_hot_ = sh;
        if (!ph) primary_press_ = false;
        if (!sh) secondary_press_ = false;
        Invalidate();
    }
}

void Dialog::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    UpdateHot(local);
}

void Dialog::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    primary_press_ = primary_hot_ && InRect(primary_rect_, local, absolute_);
    secondary_press_ = secondary_hot_ && InRect(secondary_rect_, local, absolute_);
    Invalidate();
}

void Dialog::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    const bool over_primary = primary_hot_ && InRect(primary_rect_, local, absolute_);
    const bool over_secondary = secondary_hot_ && InRect(secondary_rect_, local, absolute_);
    primary_press_ = secondary_press_ = false;
    if (over_primary && primary_action_) primary_action_();
    if (over_secondary && secondary_action_) secondary_action_();
    if (over_primary || over_secondary) Close();
    Invalidate();
}

bool Dialog::OnKey(uint32_t vk) {
    if (vk == VK_ESCAPE) {
        if (secondary_action_) secondary_action_();
        Close();
        return true;
    }
    if (vk == VK_RETURN && !primary_label_.empty()) {
        if (primary_action_) primary_action_();
        Close();
        return true;
    }
    return false;
}

} // namespace fui

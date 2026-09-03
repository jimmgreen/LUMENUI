#include "lumen/TeachingTip.h"
#include "lumen/Button.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "lumen/Theme.h"
#include "../core/window_impl.h"
#include <algorithm>

namespace lumen {
namespace {

constexpr float kPad = 14.0f;
constexpr float kClose = 28.0f;
constexpr float kGlyph = 22.0f;
constexpr float kActionH = 32.0f;
constexpr float kInf = 1.0e4f;

bool AxisFinite(float v) noexcept { return v >= 0.0f && v < kInf; }

} // namespace

TeachingTip::TeachingTip() { Clip(true); }

TeachingTip::~TeachingTip() {
    if (window_) WindowImpl::OverlayDestroyed(window_, this);
}

void TeachingTip::RelayoutParent() { Control::RelayoutParent(); }

void TeachingTip::SetTailTarget(Point window_dip, bool pointing_up) {
    tail_target_ = window_dip;
    tail_up_ = pointing_up;
}

void TeachingTip::Dismiss() {
    if (window_ && WindowImpl::TransientActive(window_, this)) {
        WindowImpl::CloseTransient(window_);
        return;
    }
    Visible(false);
    closed_.Emit();
}

TeachingTip& TeachingTip::Action(std::wstring_view label, std::function<void()> on_click) {
    action_cb_ = std::move(on_click);
    if (!action_) {
        action_ = &Add<Button>(std::wstring(label), ButtonKind::Primary);
        action_->SizeClass(ButtonSize::Small);
    } else {
        action_->Text(std::wstring(label));
        action_->Visible(!label.empty());
    }
    action_->OnClick([this] {
        if (action_cb_) action_cb_();
        Dismiss();
    });
    RelayoutParent();
    return *this;
}

Rect TeachingTip::CloseRect() const noexcept {
    if (!closable_) return {};
    return {absolute_.Right() - kPad - kClose, absolute_.y + kPad - 4.0f, kClose, kClose};
}

Size TeachingTip::Measure(Size available, const Theme& theme) {
    const float width = AxisFinite(available.w) ? std::min(width_, available.w) : width_;
    float text_w = width_ - kPad * 2.0f;
    if (!glyph_.empty()) text_w -= kGlyph + 8.0f;
    if (closable_) text_w -= kClose + 4.0f;
    text_w = std::max(80.0f, text_w);
    float h = kPad;
    if (!title_.empty()) h += std::max(20.0f, MeasureText(title_, TextRole::BodyStrong).h);
    if (!message_.empty()) {
        if (!title_.empty()) h += 6.0f;
        h += MeasureWrapped(message_, TextRole::Caption, text_w);
    }
    if (action_ && action_->Visible()) {
        MeasureChildAt(0, {text_w, theme.button_height}, theme);
        h += 12.0f + kActionH;
    }
    h += kPad;
    if (h < 56.0f) h = 56.0f;
    return {width, h};
}

void TeachingTip::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    if (!action_ || !action_->Visible()) return;
    float text_w = absolute.w - kPad * 2.0f;
    if (!glyph_.empty()) text_w -= kGlyph + 8.0f;
    if (closable_) text_w -= kClose + 4.0f;
    text_w = std::max(80.0f, text_w);
    const float btn_w = std::min(std::max(ChildDesired(0).w, 88.0f), text_w);
    float text_x = kPad;
    if (!glyph_.empty()) text_x += kGlyph + 8.0f;
    SetChildBounds(*action_, {text_x, absolute.h - kPad - kActionH, btn_w, kActionH});
    ArrangeChildAt(0);
}

void TeachingTip::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    const float radius = theme.radius_flyout;
    DrawElevated(painter, theme, absolute_, radius, Elevation::Overlay, theme.bg);

    const float tw = 9.0f;
    const float th = TailLength();
    const float cx =
        Clamp(tail_target_.x, absolute_.x + radius + tw, absolute_.Right() - radius - tw);
    Point apex, b1, b2;
    if (tail_up_) {
        apex = {cx, absolute_.y - th};
        b1 = {cx - tw, absolute_.y + 1.0f};
        b2 = {cx + tw, absolute_.y + 1.0f};
    } else {
        apex = {cx, absolute_.Bottom() + th};
        b1 = {cx - tw, absolute_.Bottom() - 1.0f};
        b2 = {cx + tw, absolute_.Bottom() - 1.0f};
    }
    painter.FillTriangle(apex, b1, b2, theme.bg);

    float text_x = absolute_.x + kPad;
    if (!glyph_.empty()) {
        const Rect glyph_box{text_x, absolute_.y + kPad, kGlyph, kGlyph};
        painter.DrawIcon(glyph_, glyph_box, 16.0f, theme.text);
        text_x += kGlyph + 8.0f;
    }
    const float text_right = closable_ ? CloseRect().x - 8.0f : absolute_.Right() - kPad;
    const float text_w = std::max(40.0f, text_right - text_x);
    float y = absolute_.y + kPad;
    if (!title_.empty()) {
        const float th_text = std::max(20.0f, MeasureText(title_, TextRole::BodyStrong).h);
        painter.DrawText(title_, {text_x, y, text_w, th_text}, TextRole::BodyStrong, theme.text);
        y += th_text;
    }
    if (!message_.empty()) {
        if (!title_.empty()) y += 6.0f;
        const float bottom = (action_ && action_->Visible()) ? action_->AbsoluteBounds().y - 8.0f
                                                             : absolute_.Bottom() - kPad;
        painter.DrawTextWrapped(message_, {text_x, y, text_w, std::max(16.0f, bottom - y)},
                                TextRole::Caption, theme.text_secondary);
    }
    if (closable_) {
        const Rect close = CloseRect();
        if (close_hot_) painter.FillRoundedRect(close, 6.0f, theme.fill_hover);
        painter.DrawIcon(icon::kClose, close, 12.0f, theme.text_secondary);
    }
}

void TeachingTip::OnMouseMove(Point local, uint32_t) {
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    const bool hot = CloseRect().Contains(world);
    if (hot != close_hot_) {
        close_hot_ = hot;
        Invalidate();
    }
}

void TeachingTip::OnMouseLeave() {
    Control::OnMouseLeave();
    close_hot_ = false;
    close_press_ = false;
    Invalidate();
}

void TeachingTip::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    close_press_ = CloseRect().Contains(world);
}

void TeachingTip::OnMouseUp(Point local, uint32_t) {
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (close_press_ && CloseRect().Contains(world) && closable_) Dismiss();
    close_press_ = false;
    Invalidate();
}

} // namespace lumen

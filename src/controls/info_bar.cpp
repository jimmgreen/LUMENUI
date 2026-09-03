#include "lumen/InfoBar.h"
#include "lumen/Button.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include <algorithm>

namespace lumen {
namespace {
constexpr float kPad = 14.0f;
constexpr float kClose = 28.0f;
constexpr float kGlyph = 32.0f;
constexpr float kActionH = 32.0f;
} // namespace

void InfoBar::RelayoutParent() { Control::RelayoutParent(); }

const wchar_t* InfoBar::GlyphToDraw() const noexcept {
    if (!glyph_.empty()) return glyph_.c_str();
    switch (tone_) {
    case InfoTone::Success:
        return icon::kCheckMark;
    case InfoTone::Warning:
        return icon::kWarning;
    case InfoTone::Critical:
        return icon::kShield;
    default:
        return icon::kInfo;
    }
}

InfoBar& InfoBar::Action(std::wstring_view label, std::function<void()> on_click) {
    action_cb_ = std::move(on_click);
    if (!action_) {
        action_ = &Add<Button>(std::wstring(label), ButtonKind::Subtle);
        action_->SizeClass(ButtonSize::Small).Height(kActionH);
        action_->Visible(!label.empty());
    } else {
        action_->Text(std::wstring(label));
        action_->Visible(!label.empty());
    }
    action_->OnClick([this] {
        if (action_cb_) action_cb_();
    });
    RelayoutParent();
    return *this;
}

Rect InfoBar::CloseRect() const noexcept {
    if (!closable_) return {};
    return {absolute_.Right() - kPad - kClose, absolute_.y + (absolute_.h - kClose) * 0.5f, kClose,
            kClose};
}

Size InfoBar::Measure(Size available, const Theme& theme) {
    float tail_w = closable_ ? kClose + 8.0f : 0.0f;
    float tail_h = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {1.0e5f, 1.0e5f}, theme);
        tail_w += desired.w + 8.0f;
        tail_h = std::max(tail_h, desired.h);
    }
    const float text_w =
        std::max(80.0f, available.w - kPad * 2.0f - kGlyph - 10.0f - tail_w);
    float text_h = 0.0f;
    if (!title_.empty()) text_h += MeasureText(title_, TextRole::BodyStrong).h;
    if (!message_.empty()) {
        if (text_h > 0.0f) text_h += 4.0f;
        text_h += MeasureWrapped(message_, TextRole::Caption, text_w);
    }
    if (text_h < 20.0f) text_h = 20.0f;
    return {std::max(available.w, 240.0f), std::max(text_h, std::max(tail_h, kGlyph)) + kPad * 2.0f};
}

void InfoBar::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    float tail = closable_ ? kClose + 8.0f : 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        tail += ChildDesired(i).w + 8.0f;
    }
    float x = absolute.w - kPad - tail;
    const float cy = absolute.h * 0.5f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size& d = ChildDesired(i);
        SetChildBounds(Child(i), {x, cy - d.h * 0.5f, d.w, d.h});
        ArrangeChildAt(i);
        x += d.w + 8.0f;
    }
}

void InfoBar::Draw(Painter& painter, const Theme& theme) {
    Color fill = theme.fill_input;
    if (tone_ == InfoTone::Warning) fill = theme.fill_input_hover;
    if (tone_ == InfoTone::Critical) fill = theme.fill_input_pressed;
    painter.FillRoundedRect(absolute_, theme.radius_control, fill);
    if (tone_ == InfoTone::Critical) {
        painter.DrawGlow(absolute_, theme.radius_control, theme.glow_sm);
    }
    painter.StrokeRoundedRect(absolute_, theme.radius_control, theme.stroke_card);

    const Rect glyph_box{absolute_.x + kPad, absolute_.y + (absolute_.h - kGlyph) * 0.5f, kGlyph,
                         kGlyph};
    painter.FillRoundedRect(glyph_box, 8.0f, theme.fill_hover);
    painter.DrawIcon(GlyphToDraw(), glyph_box, 16.0f, theme.text);

    float text_right = absolute_.Right() - kPad;
    if (closable_) text_right -= kClose + 8.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        text_right = std::min(text_right, Child(i).AbsoluteBounds().x - 8.0f);
    }
    const float text_x = glyph_box.Right() + 10.0f;
    const float text_w = std::max(40.0f, text_right - text_x);
    float y = absolute_.y + kPad;
    if (!title_.empty()) {
        painter.DrawText(title_, {text_x, y, text_w, 20.0f}, TextRole::BodyStrong, theme.text);
        y += 20.0f;
    }
    if (!message_.empty()) {
        if (!title_.empty()) y += 2.0f;
        painter.DrawTextWrapped(message_, {text_x, y, text_w, absolute_.Bottom() - y - kPad},
                                TextRole::Caption, theme.text_secondary);
    }

    if (closable_) {
        const Rect close = CloseRect();
        if (close_hot_) painter.FillRoundedRect(close, 6.0f, theme.fill_hover);
        painter.DrawIcon(icon::kClose, close, 16.0f, theme.text_secondary);
    }
}

void InfoBar::OnMouseMove(Point local, uint32_t) {
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    const bool hot = CloseRect().Contains(world);
    if (hot != close_hot_) {
        close_hot_ = hot;
        Invalidate();
    }
}

void InfoBar::OnMouseLeave() {
    Control::OnMouseLeave();
    close_hot_ = false;
    close_press_ = false;
    Invalidate();
}

void InfoBar::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    close_press_ = CloseRect().Contains(world);
}

void InfoBar::OnMouseUp(Point local, uint32_t) {
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (close_press_ && CloseRect().Contains(world) && closable_) {
        Visible(false);
        closed_.Emit();
    }
    close_press_ = false;
    Invalidate();
}

} // namespace lumen

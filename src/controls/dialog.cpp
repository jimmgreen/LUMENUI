#include "lumen/Dialog.h"
#include "lumen/Painter.h"
#include "lumen/Theme.h"
#include "lumen/Window.h"
#include "../core/text_service.h"
#include "../core/window_impl.h"
#include <algorithm>

namespace lumen {
namespace {
constexpr float kCardPad = 24.0f;
constexpr float kButtonH = 40.0f;
constexpr float kTitleTop = 20.0f;
constexpr float kTitleMinH = 28.0f;
constexpr float kGapTitleMessage = 8.0f;
constexpr float kGapAfterMessage = 16.0f;
constexpr float kGapFooter = 24.0f;
constexpr float kChildGap = 12.0f;
constexpr float kCompactW = 320.0f;
constexpr float kStandardW = 420.0f;
constexpr float kWideW = 560.0f;

float FooterButtonWidth(Button& btn, LumaTextBridge* luma) {
    const Size desired = btn.DesiredSize();
    if (desired.w > 1.0f) return std::clamp(desired.w, 96.0f, 180.0f);
    return std::clamp(MeasureUiText(btn.Text(), TextRole::Caption, 0.0f, luma).w + 32.0f, 96.0f,
                      180.0f);
}

float TitleBlockH(LumaTextBridge* luma, std::wstring_view title) {
    const std::wstring_view probe = title.empty() ? L"Ag" : title;
    return std::max(kTitleMinH, MeasureUiText(probe, TextRole::Title, 0.0f, luma).h);
}

float MessageBlockH(LumaTextBridge* luma, std::wstring_view message, float inner_w) {
    if (message.empty() || inner_w <= 0.0f) return 0.0f;
    return MeasureWrappedHeight(message, TextRole::Body, inner_w, luma);
}

float BodyStartY(LumaTextBridge* luma, std::wstring_view title, std::wstring_view message,
                 float inner_w) {
    float y = kTitleTop + TitleBlockH(luma, title) + kGapTitleMessage;
    if (!message.empty()) y += MessageBlockH(luma, message, inner_w) + kGapAfterMessage;
    return y;
}
} // namespace

Dialog::Dialog() {
    Clip(true);
    enter_.Snap(1.0f);
}

Dialog::~Dialog() {
    if (window_) WindowImpl::OverlayDestroyed(window_, this);
}

Dialog& Dialog::CardSize(DialogSize value) {
    size_ = value;
    RelayoutParent();
    return *this;
}

Dialog& Dialog::CardWidth(float dip) {
    card_width_ = std::max(0.0f, dip);
    RelayoutParent();
    return *this;
}

float Dialog::ResolvedCardWidth(float available_w) const noexcept {
    float target = kStandardW;
    if (card_width_ > 0.5f) target = card_width_;
    else if (size_ == DialogSize::Compact) target = kCompactW;
    else if (size_ == DialogSize::Wide) target = kWideW;
    return std::min(target, std::max(available_w - 24.0f, 240.0f));
}

bool Dialog::FooterVisible() const noexcept {
    return !default_close_ &&
           ((primary_btn_ && primary_btn_->Visible()) ||
            (secondary_btn_ && secondary_btn_->Visible()) ||
            (close_btn_ && close_btn_->Visible()));
}

bool Dialog::IsFooterChild(const Control& child) const noexcept {
    return &child == static_cast<const Control*>(primary_btn_) ||
           &child == static_cast<const Control*>(secondary_btn_) ||
           &child == static_cast<const Control*>(close_btn_);
}

Button* Dialog::ButtonFor(DialogCommand command) const noexcept {
    switch (command) {
    case DialogCommand::Primary: return primary_btn_;
    case DialogCommand::Secondary: return secondary_btn_;
    case DialogCommand::Close: return close_btn_;
    default: return nullptr;
    }
}

DialogCommand Dialog::ResolvedDefault() const noexcept {
    if (default_cmd_ != DialogCommand::Auto) return default_cmd_;
    if (primary_btn_ && primary_btn_->Visible()) return DialogCommand::Primary;
    if (close_btn_ && close_btn_->Visible()) return DialogCommand::Close;
    return DialogCommand::None;
}

DialogCommand Dialog::ResolvedCancel() const noexcept {
    if (cancel_cmd_ != DialogCommand::Auto) return cancel_cmd_;
    if (default_close_) return DialogCommand::None;
    if (close_btn_ && close_btn_->Visible()) return DialogCommand::Close;
    if (secondary_btn_ && secondary_btn_->Visible()) return DialogCommand::Secondary;
    return DialogCommand::None;
}

Dialog& Dialog::PrimaryButton(std::wstring_view label, std::function<void()> action) {
    primary_action_ = std::move(action);
    default_close_ = false;
    if (!primary_btn_) {
        primary_btn_ = &Add<Button>(std::wstring(label), ButtonKind::Primary);
        primary_btn_->SizeClass(ButtonSize::Small);
        primary_btn_->OnClick([this] { InvokeCommand(DialogCommand::Primary); });
    } else {
        primary_btn_->Text(label);
        primary_btn_->Visible(true);
    }
    RelayoutParent();
    return *this;
}

Dialog& Dialog::SecondaryButton(std::wstring_view label, std::function<void()> action) {
    secondary_action_ = std::move(action);
    default_close_ = false;
    if (!secondary_btn_) {
        secondary_btn_ = &Add<Button>(std::wstring(label), ButtonKind::Standard);
        secondary_btn_->SizeClass(ButtonSize::Small);
        secondary_btn_->OnClick([this] { InvokeCommand(DialogCommand::Secondary); });
    } else {
        secondary_btn_->Text(label);
        secondary_btn_->Visible(true);
    }
    RelayoutParent();
    return *this;
}

Dialog& Dialog::CloseButton(std::wstring_view label, std::function<void()> action) {
    close_action_ = std::move(action);
    default_close_ = false;
    if (!close_btn_) {
        close_btn_ = &Add<Button>(std::wstring(label), ButtonKind::Subtle);
        close_btn_->SizeClass(ButtonSize::Small);
        close_btn_->OnClick([this] { InvokeCommand(DialogCommand::Close); });
    } else {
        close_btn_->Text(label);
        close_btn_->Visible(true);
    }
    RelayoutParent();
    return *this;
}

Dialog& Dialog::DefaultClose() {
    default_close_ = true;
    if (primary_btn_) primary_btn_->Visible(false);
    if (secondary_btn_) secondary_btn_->Visible(false);
    if (close_btn_) close_btn_->Visible(false);
    primary_action_ = nullptr;
    secondary_action_ = nullptr;
    close_action_ = nullptr;
    RelayoutParent();
    return *this;
}

Dialog& Dialog::DefaultButton(DialogCommand command) {
    default_cmd_ = command;
    return *this;
}

Dialog& Dialog::CancelButton(DialogCommand command) {
    cancel_cmd_ = command;
    return *this;
}

Dialog& Dialog::OnResult(std::function<void(DialogResult)> handler) {
    result_changed_.Subscribe(std::move(handler));
    return *this;
}
Connection Dialog::BindResult(std::function<void(DialogResult)> handler) {
    return result_changed_.Connect(std::move(handler));
}

void Dialog::InvokeCommand(DialogCommand command) {
    if (closing_) return;
    switch (command) {
    case DialogCommand::Primary:
        if (primary_action_) primary_action_();
        Dismiss(DialogResult::Primary);
        break;
    case DialogCommand::Secondary:
        if (secondary_action_) secondary_action_();
        Dismiss(DialogResult::Secondary);
        break;
    case DialogCommand::Close:
        if (close_action_) close_action_();
        Dismiss(DialogResult::Close);
        break;
    case DialogCommand::None:
        Dismiss(DialogResult::None);
        break;
    default:
        break;
    }
}

void Dialog::Dismiss(DialogResult result) {
    if (closing_) return;
    closing_ = true;
    result_ = result;
    if (!window_) {
        CompleteResult();
        closing_ = false;
        return;
    }
    float dur = 0.16f;
    Ease ease = Ease::CssEaseIn;
    const Theme& theme = WindowImpl::ThemeOf(window_);
    dur = theme.duration_fast * MotionScale();
    ease = theme.ease_exit;
    if (dur <= 0.001f) {
        enter_.Snap(0.0f);
        WindowImpl::FinishDialog(window_);
        return;
    }
    enter_.Play(enter_.Value(), 0.0f, dur, ease);
    Animate();
}

void Dialog::CompleteResult() {
    if (result_signaled_) return;
    result_signaled_ = true;
    result_changed_.Emit(result_);
}

void Dialog::Close() { Dismiss(DialogResult::None); }

Size Dialog::Measure(Size available, const Theme& theme) {
    const float width = ResolvedCardWidth(available.w);
    const float inner_w = std::max(0.0f, width - kCardPad * 2.0f);
    LumaTextBridge* luma = WindowImpl::LumaOf(window_);
    float y = BodyStartY(luma, title_, message_, inner_w);
    bool extras = false;
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (!ChildVisible(i)) continue;
        Control& child = Child(i);
        if (IsFooterChild(child)) {
            MeasureChildAt(i, {180.0f, kButtonH}, theme);
            continue;
        }
        if (extras) y += kChildGap;
        extras = true;
        MeasureChildAt(i, {inner_w, 1.0e5f}, theme);
        y += std::max(0.0f, ChildDesired(i).h);
    }
    if (FooterVisible()) y += kGapFooter + kButtonH;
    y += kCardPad;
    desired_ = {width, y};
    return desired_;
}

void Dialog::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    LumaTextBridge* luma = WindowImpl::LumaOf(window_);
    const float inner_w = std::max(0.0f, absolute.w - kCardPad * 2.0f);
    float y = BodyStartY(luma, title_, message_, inner_w);
    bool extras = false;
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (!ChildVisible(i)) continue;
        Control& child = Child(i);
        if (IsFooterChild(child)) continue;
        if (extras) y += kChildGap;
        extras = true;
        const float h = std::max(0.0f, ChildDesired(i).h);
        SetChildBounds(child, {kCardPad, y, inner_w, h});
        y += h;
    }
    float x_right = absolute.w - kCardPad;
    const float footer_y = absolute.h - kCardPad - kButtonH;
    auto place = [&](Button* btn) {
        if (!btn || !btn->Visible()) return;
        const float w = FooterButtonWidth(*btn, luma);
        SetChildBounds(*btn, {x_right - w, footer_y, w, kButtonH});
        x_right -= w + 8.0f;
    };
    place(primary_btn_);
    place(secondary_btn_);
    place(close_btn_);
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        ArrangeChildAt(i);
    }
}

void Dialog::ArmEnter() {
    closing_ = false;
    result_ = DialogResult::None;
    result_signaled_ = false;
    float dur = 0.24f;
    Ease ease = Ease::CssEaseOut;
    if (window_) {
        const Theme& theme = WindowImpl::ThemeOf(window_);
        dur = theme.duration_normal * MotionScale();
        ease = theme.ease_enter;
    }
    if (dur <= 0.001f) {
        enter_.Snap(1.0f);
        return;
    }
    enter_.Play(0.0f, 1.0f, dur, ease);
    Animate();
}

bool Dialog::OnAnimate(float dt) {
    bool more = Control::OnAnimate(dt);
    if (enter_.Tick(dt)) {
        Invalidate();
        more = true;
    }
    if (closing_ && !enter_.running && window_) {
        WindowImpl::FinishDialog(window_);
        return false;
    }
    return more;
}

void Dialog::PushEnter(Painter& painter) const {
    const float t = enter_.Value();
    const float scale = 0.96f + 0.04f * t;
    const Point origin{absolute_.x + absolute_.w * 0.5f, absolute_.y + absolute_.h * 0.5f};
    painter.PushOpacity(t);
    painter.PushScale(origin, scale, scale);
}

void Dialog::PopEnter(Painter& painter) const {
    painter.PopTransform();
    painter.PopOpacity();
}

void Dialog::PushChildDraw(Painter& painter) const { PushEnter(painter); }
void Dialog::PopChildDraw(Painter& painter) const { PopEnter(painter); }

void Dialog::Draw(Painter& painter, const Theme& theme) {
    PushEnter(painter);
    const float radius = theme.radius_card;
    DrawElevated(painter, theme, absolute_, radius, Elevation::Modal, theme.fill_input);
    LumaTextBridge* luma = WindowImpl::LumaOf(window_);
    const float inner_w = std::max(0.0f, absolute_.w - kCardPad * 2.0f);
    const float title_h = TitleBlockH(luma, title_);
    painter.DrawText(title_,
                     {absolute_.x + kCardPad, absolute_.y + kTitleTop, inner_w, title_h},
                     TextRole::Title, theme.text);
    if (!message_.empty()) {
        const float msg_y = kTitleTop + title_h + kGapTitleMessage;
        const float msg_h = MessageBlockH(luma, message_, inner_w);
        painter.DrawTextWrapped(message_,
                                {absolute_.x + kCardPad, absolute_.y + msg_y, inner_w, msg_h},
                                TextRole::Body, theme.text_secondary);
    }
    PopEnter(painter);
}

bool Dialog::OnKey(uint32_t vk) {
    if (vk == VK_ESCAPE) {
        InvokeCommand(ResolvedCancel());
        return true;
    }
    if (vk == VK_RETURN) {
        const DialogCommand cmd = ResolvedDefault();
        if (cmd != DialogCommand::None && cmd != DialogCommand::Auto) {
            InvokeCommand(cmd);
            return true;
        }
    }
    return false;
}

} // namespace lumen

// overlay_host.cpp — WindowImpl 浮层：Dialog/Flyout/TeachingTip/Drawer/Busy/Toast/ToolTip。
#include "window_impl.h"
#include "log.h"
#include "lumen/BusyOverlay.h"
#include "lumen/Dialog.h"
#include "lumen/Drawer.h"
#include "lumen/Flyout.h"
#include "lumen/TeachingTip.h"
#include "lumen/ToolTip.h"
#include "lumen/Icons.h"
#include "lumen/Animate.h"
#include "lumen/Panel.h"
#include "lumen/TitleBar.h"
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {
constexpr double kToastIn = 0.22;
constexpr double kToastHold = 2.4;
constexpr double kToastStagger = 0.38;

float ToastOutSeconds(ToastMotion motion) noexcept {
    switch (motion) {
    case ToastMotion::SlideRight: return 0.36f;
    case ToastMotion::SlideDown: return 0.30f;
    case ToastMotion::Scale: return 0.22f;
    default: return 0.25f;
    }
}
}

bool WindowImpl::OverlayContains(const Control* overlay, const Control* node) {
    if (!overlay || !node) return false;
    for (const Control* p = node; p; p = p->parent_) {
        if (p == overlay) return true;
    }
    return false;
}

void WindowImpl::OverlayDestroyed(Window* window, const Control* control) {
    if (window) window->Impl()->OverlayDestroyed(control);
}

void WindowImpl::OverlayDestroyed(const Control* control) {
    if (active_busy_ == control) CloseBusy();
    else if (active_drawer_ == control) FinishDrawer();
    else if (active_dialog_ == control) FinishDialog();
    else if (active_flyout_ == control) CloseFlyout(false);
}

void WindowImpl::ShowDialog(Dialog& dialog) {
    if (owned_dialog_.get() != &dialog) owned_dialog_.reset();
    if (active_dialog_ && active_dialog_ != &dialog) FinishDialog();
    active_dialog_ = &dialog;
    dialog.parent_ = nullptr;
    BindWindowRecursive(&dialog, api_);
    dialog_focus_return_ = focused_;
    layout_dirty_ = true;
    Control* pick = nullptr;
    Control* footer = nullptr;
    for (size_t i = 0; i < dialog.ChildCount(); ++i) {
        Control& child = dialog.Child(i);
        if (!child.Visible() || !child.Focusable()) continue;
        if (dialog.IsFooterChild(child)) {
            if (!footer) footer = &child;
            continue;
        }
        pick = &child;
        break;
    }
    if (!pick) {
        if (Button* def = dialog.ButtonFor(dialog.ResolvedDefault())) pick = def;
        else pick = footer;
    }
    if (!pick) pick = FindFirstFocusable();
    if (pick) SetFocusControl(pick);
    ArmAcrylic(true);
    dialog.ArmEnter();
    Invalidate();
}

void WindowImpl::ShowDialog(DialogSpec spec) {
    auto dialog = std::make_unique<Dialog>();
    dialog->Title(spec.title).Message(spec.message).CardSize(spec.size);
    dialog->DefaultButton(spec.default_button).CancelButton(spec.cancel_button);
    if (spec.on_result) dialog->OnResult(std::move(spec.on_result));
    if (!spec.primary.label.empty()) {
        dialog->PrimaryButton(spec.primary.label, std::move(spec.primary.action));
    }
    if (!spec.secondary.label.empty()) {
        dialog->SecondaryButton(spec.secondary.label, std::move(spec.secondary.action));
    }
    if (!spec.close.label.empty()) {
        dialog->CloseButton(spec.close.label, std::move(spec.close.action));
    }
    if (spec.primary.label.empty() && spec.secondary.label.empty() && spec.close.label.empty()) {
        dialog->DefaultClose();
    }
    if (spec.content) spec.content(*dialog);
    Dialog& ref = *dialog;
    owned_dialog_ = std::move(dialog);
    ShowDialog(ref);
}

void WindowImpl::ShowDialog(std::unique_ptr<Dialog> dialog) {
    if (!dialog) return;
    Dialog& ref = *dialog;
    owned_dialog_ = std::move(dialog);
    ShowDialog(ref);
}

void WindowImpl::CloseDialog() {
    if (!active_dialog_) return;
    active_dialog_->Dismiss(DialogResult::None);
}

void WindowImpl::FinishDialog(Window* window) {
    if (window) window->Impl()->FinishDialog();
}

void WindowImpl::FinishDialog() {
    if (!active_dialog_) return;
    Dialog* closing = active_dialog_;
    closing->CompleteResult();
    Control* restore = dialog_focus_return_;
    dialog_focus_return_ = nullptr;
    if (focused_ && OverlayContains(closing, focused_)) {
        SetFocusControl(nullptr);
    }
    ForgetTree(closing);
    active_dialog_ = nullptr;
    owned_dialog_.reset();
    if (restore && restore->visible_ && restore->enabled_) {
        SetFocusControl(restore);
    }
    layout_dirty_ = true;
    ClearAcrylic();
    Invalidate();
}

void WindowImpl::ShowFlyout(Flyout& flyout, const Control* anchor) {
    ShowTransient(api_, &flyout, anchor, flyout.FlyoutWidth(),
                  flyout.PlacementWay() == FlyoutPlacement::Above);
}

void WindowImpl::ShowTeachingTip(TeachingTip& tip, const Control* anchor) {
    ShowTransient(api_, &tip, anchor, tip.TipWidth(),
                  tip.PlacementWay() == TeachingTipPlacement::Above);
}

void WindowImpl::ShowTransient(Window* window, Control* overlay, const Control* anchor,
                               float width, bool prefer_above, std::function<void()> closed) {
    if (!window || !overlay) return;
    WindowImpl* impl = window->Impl();
    Log(L"flyout show this=%p anchor=%p", static_cast<void*>(overlay),
        static_cast<const void*>(anchor));
    if (impl->active_flyout_ == overlay) return;
    impl->CloseFlyout();
    impl->active_flyout_ = overlay;
    impl->flyout_anchor_ = const_cast<Control*>(anchor);
    impl->flyout_width_ = std::max(120.0f, width);
    impl->flyout_prefer_above_ = prefer_above;
    impl->flyout_closed_ = std::move(closed);
    impl->flyout_focus_return_ = impl->focused_;
    overlay->parent_ = nullptr;
    BindWindowRecursive(overlay, window);
    impl->LayoutFlyout();
    if (overlay->Focusable()) impl->SetFocusControl(overlay);
    impl->Invalidate();
}

void WindowImpl::CloseTransient(Window* window) {
    if (window) window->Impl()->CloseFlyout();
}

bool WindowImpl::TransientActive(Window* window, const Control* overlay) {
    if (!window) return false;
    Control* active = window->Impl()->active_flyout_;
    return active && (!overlay || active == overlay);
}

void WindowImpl::CloseFlyout(bool invoke_closed) {
    if (!active_flyout_) return;
    Control* closing = active_flyout_;
    std::function<void()> closed = std::move(flyout_closed_);
    Control* restore = flyout_focus_return_;
    if (focused_ && OverlayContains(closing, focused_)) {
        SetFocusControl(nullptr);
    }
    ForgetTree(closing);
    active_flyout_ = nullptr;
    flyout_anchor_ = nullptr;
    flyout_closed_ = {};
    flyout_focus_return_ = nullptr;
    if (restore && restore->visible_ && restore->enabled_) SetFocusControl(restore);
    layout_dirty_ = true;
    Invalidate();
    if (invoke_closed) {
        if (closed) closed();
        if (auto* flyout = dynamic_cast<Flyout*>(closing)) flyout->closed_.Emit();
        else if (auto* tip = dynamic_cast<TeachingTip*>(closing)) tip->closed_.Emit();
    }
}

void WindowImpl::ShowBusy(std::wstring_view text, std::function<void()> on_cancel) {
    CloseBusy();
    owned_busy_ = std::make_unique<BusyOverlay>();
    owned_busy_->Message(text);
    if (on_cancel) owned_busy_->OnCancel(std::move(on_cancel));
    active_busy_ = owned_busy_.get();
    busy_focus_return_ = focused_;
    active_busy_->parent_ = nullptr;
    BindWindowRecursive(active_busy_, api_);
    active_busy_->ArmAnimation();
    layout_dirty_ = true;
    SetFocusControl(active_busy_);
    ArmAcrylic(true);
    Invalidate();
}

void WindowImpl::CloseBusy() {
    if (!active_busy_) return;
    BusyOverlay* closing = active_busy_;
    Control* restore = busy_focus_return_;
    busy_focus_return_ = nullptr;
    active_busy_ = nullptr;
    if (focused_ && OverlayContains(closing, focused_)) SetFocusControl(nullptr);
    ForgetTree(closing);
    owned_busy_.reset();
    if (restore && restore->visible_ && restore->enabled_) SetFocusControl(restore);
    layout_dirty_ = true;
    ClearAcrylic();
    Invalidate();
}

void WindowImpl::ShowDrawer(Drawer& drawer, Edge edge) {
    if (active_drawer_ && active_drawer_ != &drawer) FinishDrawer();
    if (active_flyout_) CloseFlyout();
    active_drawer_ = &drawer;
    drawer_focus_return_ = focused_;
    drawer.parent_ = nullptr;
    BindWindowRecursive(&drawer, api_);
    drawer.BeginOpen(edge);
    layout_dirty_ = true;
    if (Control* first = FindFirstFocusable()) SetFocusControl(first);
    ArmAcrylic(false);
    Invalidate();
}

void WindowImpl::RequestCloseDrawer() {
    if (!active_drawer_) return;
    active_drawer_->BeginClose();
}

void WindowImpl::FinishDrawer(Window* window) {
    if (window) window->Impl()->FinishDrawer();
}

void WindowImpl::FinishDrawer() {
    if (!active_drawer_) return;
    Drawer* closing = active_drawer_;
    Control* restore = drawer_focus_return_;
    drawer_focus_return_ = nullptr;
    if (focused_ && OverlayContains(closing, focused_)) SetFocusControl(nullptr);
    ForgetTree(closing);
    active_drawer_ = nullptr;
    if (restore && restore->visible_ && restore->enabled_) SetFocusControl(restore);
    layout_dirty_ = true;
    ClearAcrylic();
    Invalidate();
    closing->closed_.Emit();
}

void WindowImpl::LayoutFlyout() {
    if (!active_flyout_) return;
    const float w = client_w_ / scale_;
    const float h = client_h_ / scale_;
    const float chrome = CaptionHeight();
    const float content_h = std::max(0.0f, h - chrome);
    auto* tip = dynamic_cast<TeachingTip*>(active_flyout_);
    // surface_flyout token 是半透明面，浮在内容上会透底；弹层用不透明碳底（与菜单一致）。
    if (!tip) {
        if (auto* panel = active_flyout_->AsPanel()) {
            panel->Card(Panel::CardStyle::Input, theme_.radius_flyout);
        }
    }
    // 直调虚 Measure 不会写 desired_（那是布局容器 MeasureChildAt 的职责），必须接返回值。
    const Size desired = active_flyout_->Measure({flyout_width_, content_h}, theme_);
    const float fw = std::min({desired.w, flyout_width_, w - 16.0f});
    const float fh = std::min(std::max(desired.h, 40.0f), content_h - 16.0f);
    Rect anchor{8.0f, chrome + 8.0f, w - 16.0f, 0.0f};
    if (flyout_anchor_) anchor = flyout_anchor_->AbsoluteBounds();
    const float gap = 6.0f;
    const float tail = tip ? tip->TailLength() : 0.0f;
    float y = anchor.Bottom() + gap + tail;
    bool above = false;
    const bool prefer_above = flyout_prefer_above_;
    if (prefer_above || y + fh > chrome + content_h - 8.0f) {
        const float flipped = anchor.y - fh - gap - tail;
        if (flipped >= chrome + 4.0f || prefer_above) {
            y = flipped;
            above = true;
        }
    }
    float x = Clamp(anchor.x, 8.0f, std::max(8.0f, w - fw - 8.0f));
    if (tip) {
        const float mid = anchor.x + anchor.w * 0.5f;
        x = Clamp(mid - fw * 0.5f, 8.0f, std::max(8.0f, w - fw - 8.0f));
    }
    const float top = std::max(chrome + 4.0f, y);
    active_flyout_->Arrange({x, top, fw, fh});
    if (tip) {
        const Point target =
            flyout_anchor_ ? Point{anchor.x + anchor.w * 0.5f, above ? anchor.y : anchor.Bottom()}
                           : Point{x + fw * 0.5f, above ? top + fh : top};
        tip->SetTailTarget(target, !above);
    }
    Log(L"flyout layout desired=(%.0f,%.0f) fw=%.0f fh=%.0f rect=(%.0f,%.0f,%.0f,%.0f)",
        desired.w, desired.h, fw, fh, x, top, fw, fh);
}

void WindowImpl::SetToastMotion(ToastMotion motion) {
    toast_motion_ = motion;
}

bool WindowImpl::ToastPersist(const Toast& toast) const noexcept {
    return toast.hold_seconds <= 0.0f;
}

float WindowImpl::ToastHeight(const Toast& toast) const noexcept {
    return (toast.action.empty() && !ToastPersist(toast)) ? 40.0f : 44.0f;
}

const wchar_t* WindowImpl::ToastGlyph(const Toast& toast) const noexcept {
    if (!toast.glyph.empty()) return toast.glyph.c_str();
    return ToastKindGlyph(toast.kind);
}

void WindowImpl::BeginToastExit(Toast& toast, double now) {
    if (toast.exiting) return;
    toast.exiting = true;
    toast.exit_start = now;
    toast.hovering = false;
    toast.action_hot = false;
    toast.close_hot = false;
    RequestAnimation();
    Invalidate();
}

void WindowImpl::ClearToastHover() {
    bool dirty = false;
    for (Toast& toast : toasts_) {
        if (!toast.hovering && !toast.action_hot && !toast.close_hot) continue;
        toast.hovering = false;
        toast.action_hot = false;
        toast.close_hot = false;
        dirty = true;
    }
    if (dirty) Invalidate();
}

WindowImpl::ToastPart WindowImpl::HitToast(Point p, ptrdiff_t* index) const {
    if (index) *index = -1;
    for (size_t i = toasts_.size(); i-- > 0;) {
        const Toast& toast = toasts_[i];
        if (toast.exiting) continue;
        const double age = clock_seconds() - toast.born_seconds - toast.pause_seconds;
        if (age < 0.0) continue;
        if (!toast.close_rect.IsEmpty() && toast.close_rect.Contains(p)) {
            if (index) *index = static_cast<ptrdiff_t>(i);
            return ToastPart::Close;
        }
        if (!toast.action_rect.IsEmpty() && toast.action_rect.Contains(p)) {
            if (index) *index = static_cast<ptrdiff_t>(i);
            return ToastPart::Action;
        }
        if (toast.card.Contains(p)) {
            if (index) *index = static_cast<ptrdiff_t>(i);
            return ToastPart::Card;
        }
    }
    return ToastPart::None;
}

bool WindowImpl::UpdateToastHover(Point p) {
    ptrdiff_t index = -1;
    const ToastPart part = HitToast(p, &index);
    bool dirty = false;
    for (size_t i = 0; i < toasts_.size(); ++i) {
        Toast& toast = toasts_[i];
        const bool over = index >= 0 && static_cast<size_t>(index) == i;
        const bool action = over && part == ToastPart::Action;
        const bool close = over && part == ToastPart::Close;
        if (toast.hovering != over || toast.action_hot != action || toast.close_hot != close) {
            toast.hovering = over;
            toast.action_hot = action;
            toast.close_hot = close;
            dirty = true;
        }
    }
    if (dirty) {
        Invalidate();
        RequestAnimation();
    }
    return part != ToastPart::None;
}

void WindowImpl::ShowToast(std::wstring_view text) {
    ToastData data;
    data.text = std::wstring(text);
    data.duration = theme_.toast_duration;
    ShowToast(std::move(data));
}

void WindowImpl::ShowToast(ToastData data) {
    double born = clock_seconds();
    if (!toasts_.empty()) {
        born = std::max(born, toasts_.back().born_seconds + kToastStagger);
    }
    Toast toast;
    toast.text = std::move(data.text);
    toast.glyph = std::move(data.glyph);
    toast.action = std::move(data.action);
    toast.on_action = std::move(data.on_action);
    toast.kind = data.kind;
    toast.hold_seconds = data.duration;
    toast.born_seconds = born;
    toasts_.push_back(std::move(toast));
    RequestAnimation();
    Invalidate();
}

bool WindowImpl::TickToasts(double now_seconds) {
    bool alive = false;
    const double out = static_cast<double>(ToastOutSeconds(toast_motion_));
    for (auto it = toasts_.begin(); it != toasts_.end();) {
        if (it->exiting) {
            if (now_seconds - it->exit_start > out) {
                it = toasts_.erase(it);
                continue;
            }
            alive = true;
            ++it;
            continue;
        }
        const double age = now_seconds - it->born_seconds - it->pause_seconds;
        if (age >= 0.0 && !ToastPersist(*it) && age > kToastIn + static_cast<double>(it->hold_seconds)) {
            it->exiting = true;
            it->exit_start = now_seconds;
        }
        alive = true;
        ++it;
    }
    return alive;
}

void WindowImpl::DrawToasts(Painter& painter, const Theme& theme, const Rect& client) {
    if (toasts_.empty()) return;
    const double now = clock_seconds();
    const bool alive = TickToasts(now);
    float dt = 1.0f / 60.0f;
    if (toast_tick_ > 0.0) dt = Clamp(static_cast<float>(now - toast_tick_), 0.0f, 0.1f);
    toast_tick_ = now;

    constexpr float kWidth = 340.0f;
    constexpr float kGap = 8.0f;
    constexpr float kMargin = 20.0f;
    const float kOut = ToastOutSeconds(toast_motion_);
    const size_t n = toasts_.size();
    bool sliding = false;
    for (size_t i = 0; i < n; ++i) {
        Toast& toast = toasts_[i];
        const float kHeight = ToastHeight(toast);
        float target = client.Bottom() - kMargin;
        for (size_t k = n; k-- > i;) {
            target -= ToastHeight(toasts_[k]);
            if (k == i) break;
            target -= kGap;
        }
        const double age = now - toast.born_seconds - toast.pause_seconds;
        if (age < 0.0) {
            toast.card = {};
            toast.action_rect = {};
            toast.close_rect = {};
            continue;
        }
        if (toast.hovering && !toast.exiting) toast.pause_seconds += static_cast<double>(dt);
        if (!toast.placed) {
            toast.y = target + 16.0f;
            toast.placed = true;
        }
        sliding = EaseTo(toast.y, target, dt, 16.0f, 0.25f) || sliding;

        float alpha = 1.0f;
        float exit_x = 0.0f;
        float exit_y = 0.0f;
        float scale = 1.0f;
        if (toast.exiting) {
            const float t = Clamp(static_cast<float>((now - toast.exit_start) / kOut), 0.0f, 1.0f);
            const float e = EaseAt(t, Ease::CssEaseIn);
            switch (toast_motion_) {
            case ToastMotion::SlideRight:
                exit_x = e * (kWidth + kMargin + 16.0f);
                break;
            case ToastMotion::SlideDown:
                exit_y = e * (kHeight + 24.0f);
                alpha = 1.0f - 0.25f * e;
                break;
            case ToastMotion::Scale:
                scale = 1.0f - 0.38f * e;
                alpha = 1.0f - e;
                break;
            default:
                alpha = 1.0f - e;
                exit_y = -10.0f * e;
                break;
            }
        } else if (age < kToastIn) {
            alpha = EaseAt(static_cast<float>(age / kToastIn), Ease::CssEaseOut);
        }
        const Rect card{client.Right() - kMargin - kWidth + exit_x, toast.y + exit_y, kWidth,
                         kHeight};
        toast.card = card;
        const bool scaled = scale < 0.999f;
        if (scaled) {
            painter.PushScale({card.x + card.w * 0.5f, card.y + card.h * 0.5f}, scale, scale);
        }
        Color fill = theme.fill_input;
        if (toast.kind == ToastKind::Warning) fill = theme.fill_input_hover;
        if (toast.kind == ToastKind::Error) fill = theme.fill_input_pressed;
        if (toast.hovering && !toast.exiting && toast.kind == ToastKind::Default) {
            fill = theme.fill_input_hover;
        }
        fill.a *= alpha;
        DrawElevated(painter, theme, card, 10.0f, Elevation::Overlay, fill);
        Color glow = theme.glow_sm;
        glow.a *= alpha;
        Color ink = theme.text;
        ink.a *= alpha;
        Color muted = theme.text_secondary;
        muted.a *= alpha;

        const wchar_t* glyph = ToastGlyph(toast);
        float text_x = card.x + 14.0f;
        if (glyph) {
            const Rect glyph_box{card.x + 12.0f, card.y + (card.h - 24.0f) * 0.5f, 24.0f, 24.0f};
            Color well = theme.fill_hover;
            well.a *= alpha;
            painter.FillRoundedRect(glyph_box, 6.0f, well);
            painter.DrawIcon(glyph, glyph_box, 16.0f, ink);
            text_x = glyph_box.Right() + 8.0f;
        } else {
            Color dot = theme.accent;
            dot.a *= alpha;
            const Rect dot_rect{card.x + 16.0f, card.y + (card.h - 8.0f) * 0.5f, 8.0f, 8.0f};
            painter.DrawGlow(dot_rect, 4.0f, glow);
            painter.FillRoundedRect(dot_rect, 4.0f, dot);
            text_x = card.x + 36.0f;
        }

        float text_right = card.Right() - 14.0f;
        toast.close_rect = {};
        toast.action_rect = {};
        if (ToastPersist(toast) && !toast.exiting) {
            const Rect close{card.Right() - 10.0f - 24.0f, card.y + (card.h - 24.0f) * 0.5f, 24.0f,
                             24.0f};
            toast.close_rect = close;
            if (toast.close_hot) {
                Color wash = theme.fill_hover;
                wash.a *= alpha;
                painter.FillRoundedRect(close, 6.0f, wash);
            }
            painter.DrawIcon(icon::kClose, close, 14.0f, muted);
            text_right = close.x - 6.0f;
        }
        if (!toast.action.empty() && !toast.exiting) {
            const float action_w =
                std::max(36.0f, painter.MeasureText(toast.action, TextRole::CaptionStrong).w + 16.0f);
            const Rect action{text_right - action_w, card.y + (card.h - 28.0f) * 0.5f, action_w, 28.0f};
            toast.action_rect = action;
            if (toast.action_hot) {
                Color wash = theme.fill_hover;
                wash.a *= alpha;
                painter.FillRoundedRect(action, 6.0f, wash);
            }
            painter.DrawText(toast.action, action, TextRole::CaptionStrong, ink, Align::Center);
            text_right = action.x - 8.0f;
        }

        painter.DrawText(toast.text, {text_x, card.y, std::max(24.0f, text_right - text_x), card.h},
                         TextRole::Body, ink);
        if (scaled) painter.PopTransform();
    }
    if (alive || sliding) RequestAnimation();
    if (toasts_.empty()) toast_tick_ = 0.0;
}

void WindowImpl::HideTooltip(bool immediate) {
    ClearTooltipHover();
    if (!tooltip_shown_) {
        if (immediate) {
            tooltip_fading_ = false;
            tooltip_bubble_ = {};
            tooltip_close_ = {};
            tooltip_custom_ = nullptr;
        }
        return;
    }
    tooltip_shown_ = false;
    if (immediate) {
        tooltip_fading_ = false;
        tooltip_bubble_ = {};
        tooltip_close_ = {};
        tooltip_custom_ = nullptr;
        Invalidate();
        return;
    }
    tooltip_fading_ = true;
    tooltip_fade_start_ = clock_seconds();
    RequestAnimation();
}

void WindowImpl::OnToolTipChanged(Control* host) {
    if (tooltip_control_ != host) return;
    tooltip_custom_ = nullptr;
    HideTooltip(true);
    tooltip_control_ = nullptr;
}

void WindowImpl::ClearTooltipHover() {
    if (!tooltip_hover_) return;
    tooltip_hover_->OnMouseLeave();
    tooltip_hover_ = nullptr;
}

void WindowImpl::SetTooltipHover(Control* hit, Point p, uint32_t buttons) {
    if (hit != tooltip_hover_) {
        if (tooltip_hover_) {
            tooltip_hover_->mouse_local_ = ToLocal(tooltip_hover_, p);
            tooltip_hover_->OnMouseLeave();
        }
        tooltip_hover_ = hit;
        if (tooltip_hover_) {
            tooltip_hover_->mouse_local_ = ToLocal(tooltip_hover_, p);
            tooltip_hover_->OnMouseEnter();
        }
        Invalidate();
    }
    if (tooltip_hover_) {
        tooltip_hover_->mouse_local_ = ToLocal(tooltip_hover_, p);
        tooltip_hover_->OnMouseMove(ToLocal(tooltip_hover_, p), buttons);
    }
}

float WindowImpl::TickTooltip(double now) {
    if (tooltip_shown_) {
        const float a = static_cast<float>(std::clamp((now - tooltip_born_) / 0.12, 0.0, 1.0));
        if (a < 1.0f) RequestAnimation();
        return a;
    }
    if (tooltip_fading_) {
        const float a =
            1.0f - static_cast<float>(std::clamp((now - tooltip_fade_start_) / 0.10, 0.0, 1.0));
        if (a > 0.0f) {
            RequestAnimation();
            return a;
        }
        tooltip_fading_ = false;
        tooltip_bubble_ = {};
        tooltip_close_ = {};
        tooltip_custom_ = nullptr;
    }
    if (tooltip_control_) RequestAnimation();   // dwell 尚未到点：继续等到阈值
    return 0.0f;
}

void WindowImpl::DrawTooltip(Painter& painter, const Theme& theme, const Rect& client) {
    const double now = clock_seconds();
    if (tooltip_control_ && tooltip_control_ != tooltip_suppressed_ &&
        !tooltip_shown_ && !tooltip_fading_ &&
        now - tooltip_dwell_start_ >= static_cast<double>(theme.tooltip_delay)) {
        tooltip_shown_ = true;
        tooltip_fading_ = false;
        tooltip_text_ = tooltip_control_->tooltip_;
        tooltip_custom_ = tooltip_control_->tooltip_content_.get();
        tooltip_anchor_bounds_ = tooltip_control_->AbsoluteBounds();
        tooltip_born_ = now;
    }
    const float alpha = TickTooltip(now);
    if (alpha <= 0.0f || !TooltipHasPayload()) {
        if (alpha <= 0.0f) {
            tooltip_bubble_ = {};
            tooltip_close_ = {};
        }
        return;
    }

    constexpr float kPad = 16.0f;
    constexpr float kMaxText = 268.0f;
    constexpr float kClose = 20.0f;
    constexpr float kCloseGap = 8.0f;
    constexpr float kTitleH = 22.0f;
    constexpr float kTitleGap = 8.0f;
    constexpr float kArrowW = 14.0f;
    constexpr float kArrowH = 7.0f;
    constexpr float kGap = 4.0f;
    constexpr float kInset = 8.0f;
    constexpr float kJoin = 1.5f;   // 底边伸进气泡，盖住描边拼缝

    const bool custom = tooltip_custom_ != nullptr;
    std::wstring_view title;
    std::wstring_view body;
    bool has_title = false;
    bool closable = false;
    float inner_w = kMaxText;
    float content_h = 0.0f;
    float body_h = 0.0f;
    float content_w = kMaxText;

    if (custom) {
        closable = tooltip_custom_->Closable();
        const float cap = tooltip_custom_->MaxWidth();
        const float close_reserve = closable ? kClose + kCloseGap : 0.0f;
        Size desired =
            tooltip_custom_->Measure({std::max(8.0f, cap - close_reserve), 1.0e5f}, theme);
        inner_w = desired.w + close_reserve;
        if (closable) inner_w = std::max(inner_w, 160.0f);
        inner_w = std::min(inner_w, cap);
        inner_w = std::max(inner_w, 8.0f);
        content_w = std::max(8.0f, inner_w - close_reserve);
        desired = tooltip_custom_->Measure({content_w, 1.0e5f}, theme);
        content_h = std::max(desired.h, closable ? kClose : 8.0f);
    } else {
        const std::wstring_view full = tooltip_text_;
        body = full;
        const size_t nl = full.find(L'\n');
        if (nl != std::wstring_view::npos) {
            size_t title_end = nl;
            if (title_end > 0 && full[title_end - 1] == L'\r') --title_end;
            title = full.substr(0, title_end);
            size_t body_start = nl + 1;
            if (body_start < full.size() && full[body_start] == L'\r') ++body_start;
            body = full.substr(body_start);
        }
        has_title = !title.empty();
        closable = has_title;

        if (has_title) {
            const float title_w = painter.MeasureText(title, TextRole::BodyStrong).w;
            const float body_w = body.empty() ? 0.0f : painter.MeasureText(body, TextRole::Body).w;
            inner_w = title_w + kCloseGap + kClose;
            inner_w = std::max(inner_w, std::min(body_w, kMaxText));
            inner_w = std::max(inner_w, 160.0f);
            inner_w = std::min(inner_w, kMaxText);
        } else {
            const float body_w = body.empty() ? 0.0f : painter.MeasureText(body, TextRole::Body).w;
            inner_w = std::min(kMaxText, std::max(body_w, 8.0f));
        }
        const float body_wrap = inner_w;
        body_h = body.empty() ? 0.0f : painter.MeasureTextWrapped(body, TextRole::Body, body_wrap);
        if (!body.empty() && body_h < 1.0f) {
            body_h = painter.MeasureText(L"M", TextRole::Body).h;
        }
        content_h = (has_title ? kTitleH + kTitleGap : 0.0f) + body_h;
        content_w = inner_w;
    }

    Rect bubble{0.0f, 0.0f, kPad * 2.0f + inner_w, kPad * 2.0f + content_h};

    const float anchor_cx = tooltip_anchor_bounds_.x + tooltip_anchor_bounds_.w * 0.5f;
    bubble.x = anchor_cx - bubble.w * 0.5f;
    const float max_x = std::max(kInset, client.Right() - bubble.w - kInset);
    bubble.x = Clamp(bubble.x, kInset, max_x);

    tooltip_arrow_down_ = true;
    bubble.y = tooltip_anchor_bounds_.y - kGap - kArrowH - bubble.h;
    if (bubble.y < client.y + kInset) {
        tooltip_arrow_down_ = false;
        bubble.y = tooltip_anchor_bounds_.Bottom() + kGap + kArrowH;
    }
    const float max_y = std::max(kInset, client.Bottom() - bubble.h - kInset);
    bubble.y = Clamp(bubble.y, kInset, max_y);

    const float radius = theme.radius_flyout;
    const float tip_x =
        Clamp(anchor_cx, bubble.x + radius + kInset, bubble.Right() - radius - kInset);

    Point left{};
    Point tip{};
    Point right{};
    if (tooltip_arrow_down_) {
        left = {tip_x - kArrowW * 0.5f, bubble.Bottom() - kJoin};
        right = {tip_x + kArrowW * 0.5f, bubble.Bottom() - kJoin};
        tip = {tip_x, bubble.Bottom() + kArrowH};
    } else {
        left = {tip_x - kArrowW * 0.5f, bubble.y + kJoin};
        right = {tip_x + kArrowW * 0.5f, bubble.y + kJoin};
        tip = {tip_x, bubble.y - kArrowH};
    }

    tooltip_bubble_ = bubble;
    if (closable) {
        tooltip_close_ = {bubble.Right() - kPad - kClose,
                          bubble.y + kPad + (kTitleH - kClose) * 0.5f, kClose, kClose};
    } else {
        tooltip_close_ = {};
    }

    if (custom) {
        tooltip_custom_->Arrange({bubble.x + kPad, bubble.y + kPad, content_w, content_h});
    }

    Color fill = theme.surface_flyout;
    fill.a *= alpha;
    Color stroke = theme.stroke_card;
    stroke.a *= alpha;
    Color title_color = theme.text;
    title_color.a *= alpha;
    Color body_color = theme.text_secondary;
    body_color.a *= alpha;

    DrawElevated(painter, theme, bubble, radius, Elevation::Overlay, fill);
    painter.FillTriangle(left, tip, right, fill);
    // 再填一次箭头，盖住圆角描边在底边上切出的拼缝。
    painter.FillTriangle(left, tip, right, fill);
    painter.StrokePolyline(left, tip, right, stroke, 1.0f);

    if (custom) {
        DrawControlTree(painter, theme, tooltip_custom_);
        if (closable) {
            Color close_color = theme.text_secondary;
            close_color.a *= alpha;
            painter.DrawIcon(icon::kClose, tooltip_close_, 16.0f, close_color);
        }
        return;
    }

    if (has_title) {
        const Rect title_rect{bubble.x + kPad, bubble.y + kPad, inner_w - kClose - kCloseGap,
                              kTitleH};
        painter.DrawTextGlow(title, title_rect, TextRole::BodyStrong, title_color);
        Color close_color = theme.text_secondary;
        close_color.a *= alpha;
        painter.DrawIcon(icon::kClose, tooltip_close_, 16.0f, close_color);
    }
    if (!body.empty()) {
        const float body_y = bubble.y + kPad + (has_title ? kTitleH + kTitleGap : 0.0f);
        painter.DrawTextWrapped(body, {bubble.x + kPad, body_y, inner_w, body_h},
                                TextRole::Body, body_color);
    }
}

void WindowImpl::ArmAcrylic(bool tween) {
    painter_.InvalidateAcrylic();
    if (!tween) {
        acrylic_tween_.Snap(1.0f);
        return;
    }
    const float dur = theme_.duration_normal * theme_.motion_scale;
    if (dur < 0.01f) {
        acrylic_tween_.Snap(1.0f);
        return;
    }
    acrylic_tween_.Play(0.0f, 1.0f, dur, theme_.ease_enter);
    RequestAnimation();
}

void WindowImpl::ClearAcrylic() {
    painter_.InvalidateAcrylic();
    acrylic_tween_.Snap(0.0f);
}

bool WindowImpl::OverlayWantsAcrylic() const noexcept {
    return active_dialog_ != nullptr || active_busy_ != nullptr || active_drawer_ != nullptr;
}

float WindowImpl::AcrylicAmount() const noexcept {
    if (active_dialog_ || active_busy_) return acrylic_tween_.Value();
    if (active_drawer_) return active_drawer_->slide_.Value();
    return 0.0f;
}

float WindowImpl::AcrylicDim() const noexcept {
    if (active_dialog_) return 0.48f;
    if (active_busy_) return 0.40f;
    if (active_drawer_) return 0.35f;
    return 0.0f;
}

bool WindowImpl::TooltipHit(Point p) const {
    if (tooltip_bubble_.IsEmpty()) return false;
    if (tooltip_bubble_.Contains(p)) return true;
    constexpr float kArrowH = 7.0f;
    constexpr float kGap = 4.0f;
    const float extra = kArrowH + kGap;
    if (tooltip_arrow_down_) {
        return Rect{tooltip_bubble_.x, tooltip_bubble_.Bottom(), tooltip_bubble_.w, extra}
            .Contains(p);
    }
    return Rect{tooltip_bubble_.x, tooltip_bubble_.y - extra, tooltip_bubble_.w, extra}.Contains(p);
}

} // namespace lumen

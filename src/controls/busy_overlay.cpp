#include "lumen/BusyOverlay.h"
#include "lumen/App.h"
#include "lumen/Button.h"
#include "lumen/Label.h"
#include "lumen/Painter.h"
#include "lumen/Theme.h"
#include "lumen/ProgressRing.h"
#include "lumen/Window.h"
#include "../core/window_impl.h"

namespace lumen {

BusyOverlay::BusyOverlay() {
    AlignCross(CrossAlign::Center);
    Spacing(12.0f);
    Padding(28.0f, 24.0f);
    ring_ = &Add<ProgressRing>();
    ring_->Indeterminate(true).Box(36.0f);
    const auto& strings = App::Strings();
    label_ = &Add<Label>(strings.busy, TextRole::Body);
    cancel_btn_ = &Add<Button>(strings.busy_cancel, ButtonKind::Subtle);
    cancel_btn_->SizeClass(ButtonSize::Small);
    cancel_btn_->Visible(false);
    cancel_btn_->OnClick([this] {
        cancel_sig_.Emit();
        RequestClose();
    });
}

BusyOverlay::~BusyOverlay() {
    if (window_) WindowImpl::OverlayDestroyed(window_, this);
}

BusyOverlay& BusyOverlay::Message(std::wstring_view value) {
    if (label_) label_->Text(value);
    Relayout();
    return *this;
}

const std::wstring& BusyOverlay::Message() const noexcept {
    static const std::wstring kEmpty;
    return label_ ? label_->Text() : kEmpty;
}

BusyOverlay& BusyOverlay::OnCancel(std::function<void()> handler) {
    cancel_sig_.Subscribe(std::move(handler));
    if (cancel_btn_) cancel_btn_->Visible(!cancel_sig_.Empty());
    Relayout();
    return *this;
}
Connection BusyOverlay::BindCancel(std::function<void()> handler) {
    return cancel_sig_.Connect(std::move(handler));
}

void BusyOverlay::Draw(Painter& painter, const Theme& theme) {
    DrawElevated(painter, theme, absolute_, 16.0f, Elevation::Overlay, theme.fill_input);
}

void BusyOverlay::RequestClose() {
    if (!window_) return;
    Window* host = window_;
    host->Post([host] { host->CloseBusy(); });
}

void BusyOverlay::ArmAnimation() {
    if (ring_) WindowImpl::Animate(window_, ring_);
}

bool BusyOverlay::OnKey(uint32_t vk) {
    if (vk == 0x1B && !cancel_sig_.Empty()) {   // VK_ESCAPE
        cancel_sig_.Emit();
        RequestClose();
        return true;
    }
    return false;
}

} // namespace lumen

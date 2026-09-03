#include "lumen/Drawer.h"
#include "lumen/Painter.h"
#include "lumen/Theme.h"
#include "lumen/Window.h"
#include "../core/window_impl.h"

namespace lumen {

Drawer::Drawer() {
    Comfortable();
    Spacing(12.0f);
    Clip(true);
}

Drawer::~Drawer() {
    if (window_) WindowImpl::OverlayDestroyed(window_, this);
}

void Drawer::BeginOpen(Edge edge) {
    edge_ = edge;
    closing_ = false;
    slide_.Play(0.0f, 1.0f, 0.22f, Ease::CssEaseOut);
    Animate();
}

void Drawer::BeginClose() {
    if (closing_) return;
    closing_ = true;
    slide_.Play(slide_.Value(), 0.0f, 0.18f, Ease::CssEaseIn);
    Animate();
}

bool Drawer::OnAnimate(float dt_seconds) {
    if (slide_.Tick(dt_seconds)) {
        if (window_) WindowImpl::Relayout(window_);
        Invalidate();
        return true;
    }
    if (closing_ && window_) {
        WindowImpl::FinishDrawer(window_);
        return false;
    }
    return Control::OnAnimate(dt_seconds);
}

bool Drawer::OnKey(uint32_t vk) {
    if (vk == 0x1B) {   // VK_ESCAPE
        BeginClose();
        return true;
    }
    return false;
}

void Drawer::Draw(Painter& painter, const Theme& theme) {
    DrawElevated(painter, theme, absolute_, 0.0f, Elevation::Overlay, theme.bg);
}

} // namespace lumen

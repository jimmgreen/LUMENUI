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
    // 入场 duration_normal、退场 duration_fast（退场略快），随 motion_scale 缩放；
    // 系统关客户区动画时直接到位，收尾布局由 ShowDrawer 的 layout_dirty_ 完成。
    const float dur = window_ ? WindowImpl::ThemeOf(window_).duration_normal * MotionScale() : 0.0f;
    if (dur > 0.001f) {
        sliding_ = true;
        slide_.Play(0.0f, 1.0f, dur, Ease::CssEaseOut);
        Animate();
    } else {
        sliding_ = false;
        slide_.Snap(1.0f);
    }
}

void Drawer::BeginClose() {
    if (closing_) return;
    closing_ = true;
    const float dur = window_ ? WindowImpl::ThemeOf(window_).duration_fast * MotionScale() : 0.0f;
    if (dur > 0.001f) {
        sliding_ = true;
        slide_.Play(slide_.Value(), 0.0f, dur, Ease::CssEaseIn);
        Animate();
        return;
    }
    // 无动画：跳过终帧等待，直接走完关闭收尾（焦点恢复 + closed 事件）。
    slide_.Snap(0.0f);
    sliding_ = false;
    closing_ = false;
    if (window_) WindowImpl::FinishDrawer(window_);
}

bool Drawer::OnAnimate(float dt_seconds) {
    const bool was_sliding = sliding_;
    // 基类（聚光/焦点环）与滑动并行推进，不得被提前 return 短路。
    const bool base_more = Control::OnAnimate(dt_seconds);
    if (AdvanceAnimation(slide_, dt_seconds)) {
        if (window_) WindowImpl::Relayout(window_);
        Invalidate();
        return true;
    }
    if (was_sliding && window_) {
        // Tween::Tick 完成帧返回 false，终帧不会走上面的 Relayout；不补一次，
        // 抽屉会停在倒数第二帧的 t 上，右侧内容被窗口边缘裁掉。
        sliding_ = false;
        WindowImpl::Relayout(window_);
        Invalidate();
        return true;
    }
    if (closing_ && window_) {
        WindowImpl::FinishDrawer(window_);
        return false;
    }
    return base_more;
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

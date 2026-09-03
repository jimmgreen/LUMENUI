#include "lumen/RepeatButton.h"
#include <windows.h>

namespace lumen {

bool RepeatButton::Inside(Point local) const noexcept {
    if (absolute_.w <= 0.5f || absolute_.h <= 0.5f) return true;
    return local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
           local.y <= absolute_.h;
}

void RepeatButton::Fire() {
    if (enabled_) click_.Emit();
}

void RepeatButton::BeginHold(bool from_key, Point local) {
    pressed_ = true;
    key_held_ = from_key;
    pointer_inside_ = from_key || Inside(local);
    Fire();
    hold_.Press();
    Animate();
}

void RepeatButton::StopHold() {
    hold_.Release();
    key_held_ = false;
    pointer_inside_ = false;
    if (pressed_) {
        pressed_ = false;
        Animate();
    }
}

void RepeatButton::OnMouseDown(Point local, uint32_t buttons) {
    if (!enabled_) return;
    if (buttons && !(buttons & MK_LBUTTON)) return;
    BeginHold(false, local);
}

void RepeatButton::OnMouseUp(Point, uint32_t) {
    StopHold();
}

void RepeatButton::OnMouseMove(Point local, uint32_t) {
    if (!hold_.armed || key_held_) return;
    const bool inside = Inside(local);
    if (inside == pointer_inside_) return;
    pointer_inside_ = inside;
    pressed_ = inside;
    Animate();
}

void RepeatButton::OnMouseLeave() {
    Button::OnMouseLeave();
    if (hold_.armed && !key_held_) pointer_inside_ = false;
}

void RepeatButton::OnFocusChanged(bool focused) {
    Button::OnFocusChanged(focused);
    if (!focused) StopHold();
}

bool RepeatButton::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk != VK_SPACE && vk != VK_RETURN) return false;
    if (key_held_ || hold_.armed) return true;
    BeginHold(true, {});
    return true;
}

bool RepeatButton::OnAnimate(float dt) {
    bool active = Button::OnAnimate(dt);
    if (!hold_.armed) return active;
    if (!enabled_) {
        StopHold();
        return active;
    }
    if (key_held_) {
        const bool down =
            (GetKeyState(VK_SPACE) & 0x8000) != 0 || (GetKeyState(VK_RETURN) & 0x8000) != 0;
        if (!down) {
            StopHold();
            return true;
        }
    }
    const bool live = key_held_ || pointer_inside_;
    const int n = hold_.Tick(dt, live);
    for (int i = 0; i < n; ++i) Fire();
    return true;
}

} // namespace lumen

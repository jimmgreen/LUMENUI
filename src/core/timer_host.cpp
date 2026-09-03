// timer_host.cpp — WindowImpl 业务定时器（WM_TIMER，不抢 vsync）。
#include "window_impl.h"
#include <algorithm>

namespace lumen {

Window::TimerId WindowImpl::SetInterval(float seconds, std::function<void()> fn, bool once) {
    if (!hwnd_ || !fn) return 0;
    const UINT ms = static_cast<UINT>(std::max(1.0f, seconds * 1000.0f));
    const UINT_PTR id = next_timer_id_++;
    if (!SetTimer(hwnd_, id, ms, nullptr)) return 0;
    timers_.push_back(TimerSlot{id, std::move(fn), once});
    return static_cast<Window::TimerId>(id);
}

void WindowImpl::ClearTimer(Window::TimerId id) {
    if (id == 0) return;
    const UINT_PTR native = static_cast<UINT_PTR>(id);
    if (hwnd_) KillTimer(hwnd_, native);
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                 [native](const TimerSlot& slot) { return slot.id == native; }),
                  timers_.end());
}

void WindowImpl::FireTimer(UINT_PTR id) {
    std::function<void()> fn;
    bool once = false;
    for (TimerSlot& slot : timers_) {
        if (slot.id == id) {
            fn = slot.fn;
            once = slot.once;
            break;
        }
    }
    if (once) ClearTimer(static_cast<Window::TimerId>(id));
    if (fn) fn();
}

} // namespace lumen

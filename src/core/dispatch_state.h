// dispatch_state.h — 队列生命期和唤醒合并；回调销毁不得发生在锁内。
#pragma once
#include "lumen/Dispatcher.h"
#include <atomic>
#include <deque>
#include <mutex>

namespace lumen {
struct DispatchState {
    std::mutex mutex;
    std::deque<std::function<void()>> queue;
    std::atomic<void*> target{nullptr};
    std::function<bool(void*)> wake;
    bool closed = false, wake_queued = false;

    PostResult Post(std::function<void()> fn) {
        std::unique_lock<std::mutex> lock(mutex);
        if (closed) return PostResult::Closed;
        if (!fn) return PostResult::Accepted;
        queue.push_back(std::move(fn));
        if (!wake_queued) {
            const auto hwnd = target.load(std::memory_order_acquire);
            if (!hwnd || !wake || !wake(hwnd)) {
                auto rejected = std::move(queue.back());
                queue.pop_back();
                lock.unlock();
                return PostResult::WakeFailed;
            }
            wake_queued = true;
        }
        return PostResult::Accepted;
    }
    bool TryDrain(std::deque<std::function<void()>>& out) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        out.swap(queue);
        wake_queued = false;
        return true;
    }
    void Close() {
        std::deque<std::function<void()>> dropped;
        {
            std::lock_guard<std::mutex> lock(mutex);
            closed = true;
            queue.swap(dropped);
            wake_queued = false;
            target.store(nullptr, std::memory_order_release);
        }
    }
};
}

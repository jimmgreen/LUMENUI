// lumen/UpdateScope.h — UI 线程嵌套批量更新；值立即变化，通知和绘制合并。
// Events: 无；回调异常在正常退出时传播，异常展开时保留原异常。
// Keys: 无
// Layout: 非控件；不可跨线程或跨协程暂停持有。
#pragma once
#include <exception>
#include <functional>
#include <utility>
#include <vector>

namespace lumen {
class UpdateScope {
public:
    UpdateScope() : exceptions_(std::uncaught_exceptions()) { ++Current().depth; }
    UpdateScope(const UpdateScope&) = delete;
    UpdateScope& operator=(const UpdateScope&) = delete;
    ~UpdateScope() noexcept(false) {
        if (--Current().depth != 0) return;
        if (std::uncaught_exceptions() > exceptions_) { try { Flush(); } catch (...) {} }
        else Flush();
    }
    static bool Active() { return Current().depth > 0 || Current().flushing; }
    static bool Defer(const void* key, std::function<void()> fn) {
        auto& state = Current();
        if (!Active()) return false;
        for (auto& entry : state.pending) if (entry.key == key) {
            entry.fn = std::move(fn);
            return true;
        }
        state.pending.push_back({key, std::move(fn)});
        return true;
    }
private:
    struct Entry { const void* key; std::function<void()> fn; };
    struct State { int depth = 0; bool flushing = false; std::vector<Entry> pending; };
    static State& Current() { thread_local State state; return state; }
    static void Flush() {
        auto& state = Current();
        if (state.flushing) return;
        state.flushing = true;
        std::exception_ptr error;
        while (!state.pending.empty()) {
            std::vector<Entry> batch;
            batch.swap(state.pending);
            for (auto& entry : batch) {
                try { if (entry.fn) entry.fn(); }
                catch (...) { if (!error) error = std::current_exception(); }
            }
        }
        state.flushing = false;
        if (error) std::rethrow_exception(error);
    }
    int exceptions_;
};
}

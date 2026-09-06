// lumen/Dispatcher.h — 可跨线程持有的 UI 投递端口，不借用 Window 对象。
// Events: 无；Post 回调在目标 UI 线程执行。
// Keys: 无
// Layout: 非控件
#pragma once
#include <functional>
#include <utility>

namespace lumen {
enum class PostResult { Accepted, Closed, WakeFailed };

class UiDispatcher {
public:
    UiDispatcher() = default;
    // 始终排队；Accepted 仍可在窗口关闭时被丢弃。拒绝的回调绝不执行。
    PostResult Post(std::function<void()> fn) const {
        return post_ ? post_(std::move(fn)) : PostResult::Closed;
    }
    explicit operator bool() const noexcept { return static_cast<bool>(post_); }
private:
    friend class Window;
    explicit UiDispatcher(std::function<PostResult(std::function<void()>)> post)
        : post_(std::move(post)) {}
    std::function<PostResult(std::function<void()>)> post_;
};
}

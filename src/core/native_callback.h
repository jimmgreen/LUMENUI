#pragma once
#include <atomic>

namespace lumen {
namespace detail {
inline std::atomic<unsigned> native_callbacks{0};
inline std::atomic<unsigned> native_objects{0};
}

// 整个原生入口及嵌套消息会话在途；成员析构和 delete this 必须先于此守卫退出。
class NativeCallbackScope {
public:
    NativeCallbackScope() noexcept { detail::native_callbacks.fetch_add(1, std::memory_order_acq_rel); }
    ~NativeCallbackScope() { detail::native_callbacks.fetch_sub(1, std::memory_order_acq_rel); }
    NativeCallbackScope(const NativeCallbackScope&) = delete;
    NativeCallbackScope& operator=(const NativeCallbackScope&) = delete;
};

// OLE 客户端可能在窗口销毁后仍持有接口；首个成员最后析构，覆盖其余成员清理。
class NativeObjectLifetime {
public:
    NativeObjectLifetime() noexcept { detail::native_objects.fetch_add(1, std::memory_order_acq_rel); }
    ~NativeObjectLifetime() { detail::native_objects.fetch_sub(1, std::memory_order_acq_rel); }
    NativeObjectLifetime(const NativeObjectLifetime&) = delete;
    NativeObjectLifetime& operator=(const NativeObjectLifetime&) = delete;
};
} // namespace lumen

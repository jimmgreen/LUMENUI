// com_ptr.h — 极简 COM 智能指针。
#pragma once

namespace fui {

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { if (p) p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { if (p) p->Release(); p = o.p; o.p = nullptr; }
        return *this;
    }
    T** operator&() noexcept { return &p; }
    T* operator->() const noexcept { return p; }
    explicit operator bool() const noexcept { return p != nullptr; }
    T* get() const noexcept { return p; }
    T* detach() noexcept { T* tmp = p; p = nullptr; return tmp; }
    void reset() noexcept { if (p) { p->Release(); p = nullptr; } }
};

} // namespace fui

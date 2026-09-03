// lumen/Signal.h — 多订阅事件与可断开连接。Emit 把槽拷到栈上再调用，迭代中 Connect/Disconnect 安全。
// Events: OnChanged
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace lumen {

class Connection;

template<class... Args>
class Signal {
public:
    using Fn = std::function<void(Args...)>;

    Connection Connect(Fn fn);
    // 订阅随 Signal 生命周期，不返回句柄；OnX 链式注册走这条。
    void Subscribe(Fn fn);
    void Disconnect(uint64_t id) noexcept;
    void Clear() noexcept { slots_.clear(); }
    void Emit(const Args&... args) const;
    bool Empty() const noexcept { return slots_.empty(); }

private:
    struct Slot {
        uint64_t id = 0;
        Fn fn;
    };
    std::vector<Slot> slots_;
    uint64_t next_ = 1;
};

class Connection {
public:
    Connection() noexcept = default;
    Connection(void (*erase)(void*, uint64_t), void* ctx, uint64_t id) noexcept
        : erase_(erase), signal_(ctx), id_(id) {}
    template<class... Args>
    Connection(Signal<Args...>* signal, uint64_t id) noexcept
        : erase_([](void* p, uint64_t i) {
              static_cast<Signal<Args...>*>(p)->Disconnect(i);
          }),
          signal_(signal), id_(id) {}
    Connection(Connection&& other) noexcept { *this = std::move(other); }
    Connection& operator=(Connection&& other) noexcept {
        if (this == &other) return *this;
        Disconnect();
        erase_ = other.erase_;
        signal_ = other.signal_;
        id_ = other.id_;
        other.erase_ = nullptr;
        other.signal_ = nullptr;
        other.id_ = 0;
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { Disconnect(); }

    void Disconnect() noexcept {
        if (erase_ && signal_ && id_) erase_(signal_, id_);
        erase_ = nullptr;
        signal_ = nullptr;
        id_ = 0;
    }
    // 放手：析构不再断开，槽随 Signal 存活。
    void Release() noexcept {
        erase_ = nullptr;
        signal_ = nullptr;
        id_ = 0;
    }
    explicit operator bool() const noexcept { return signal_ != nullptr && id_ != 0; }

private:
    void (*erase_)(void*, uint64_t) = nullptr;
    void* signal_ = nullptr;
    uint64_t id_ = 0;
};

class ScopedConnection {
public:
    ScopedConnection() noexcept = default;
    explicit ScopedConnection(Connection c) noexcept : conn_(std::move(c)) {}
    ScopedConnection(ScopedConnection&&) noexcept = default;
    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this == &other) return *this;
        conn_.Disconnect();
        conn_ = std::move(other.conn_);
        return *this;
    }
    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;
    ~ScopedConnection() { conn_.Disconnect(); }
    void Disconnect() noexcept { conn_.Disconnect(); }
    Connection Release() noexcept { return std::move(conn_); }
    explicit operator bool() const noexcept { return static_cast<bool>(conn_); }

private:
    Connection conn_;
};

template<class... Args>
Connection Signal<Args...>::Connect(Fn fn) {
    if (!fn) return {};
    const uint64_t id = next_++;
    slots_.push_back(Slot{id, std::move(fn)});
    return Connection(this, id);
}

template<class... Args>
void Signal<Args...>::Subscribe(Fn fn) {
    Connect(std::move(fn)).Release();
}

template<class... Args>
void Signal<Args...>::Disconnect(uint64_t id) noexcept {
    if (id == 0) return;
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->id == id) {
            slots_.erase(it);
            return;
        }
    }
}

template<class... Args>
void Signal<Args...>::Emit(const Args&... args) const {
    const size_t n = slots_.size();
    if (n == 0) return;
    constexpr size_t kStack = 8;
    if (n <= kStack) {
        std::array<Fn, kStack> copy{};
        for (size_t i = 0; i < n; ++i) copy[i] = slots_[i].fn;
        for (size_t i = 0; i < n; ++i) {
            if (copy[i]) copy[i](args...);
        }
        return;
    }
    std::vector<Fn> copy;
    copy.reserve(n);
    for (const Slot& slot : slots_) copy.push_back(slot.fn);
    for (Fn& fn : copy) {
        if (fn) fn(args...);
    }
}

template<class T>
class Property {
public:
    Property() = default;
    explicit Property(T value) : value_(std::move(value)) {}

    const T& Get() const noexcept { return value_; }
    operator const T&() const noexcept { return value_; }

    Property& operator=(T value) {
        if (value_ == value) return *this;
        value_ = std::move(value);
        changed_.Emit(value_);
        return *this;
    }
    void Set(T value) { *this = std::move(value); }

    Connection OnChanged(std::function<void(const T&)> fn) { return changed_.Connect(std::move(fn)); }
    Signal<const T&>& Changed() noexcept { return changed_; }

private:
    T value_{};
    Signal<const T&> changed_;
};

template <class T>
class Computed {
public:
    template <class Fn, class... P>
    Computed(Fn fn, Property<P>&... deps) {
        auto recompute = [this, fn] { value_.Set(fn()); };
        (deps_.push_back(ScopedConnection(deps.OnChanged([recompute](const auto&) { recompute(); }))), ...);
        recompute();
    }
    const T& Get() const noexcept { return value_.Get(); }
    operator const T&() const noexcept { return Get(); }
    Connection OnChanged(std::function<void(const T&)> fn) { return value_.OnChanged(std::move(fn)); }
    Property<T>& AsProperty() noexcept { return value_; }

private:
    Property<T> value_;
    std::vector<ScopedConnection> deps_;
};

} // namespace lumen

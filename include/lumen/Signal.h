// lumen/Signal.h — 多订阅事件与可断开连接。Emit 不复制回调，迭代中 Connect/Disconnect 安全；
// 事件源先析构时连接自动失效（断开成为 no-op），订阅者后析构不再访问已释放的源。
// Events: OnChanged
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <tuple>
#include "UpdateScope.h"

namespace lumen {

class Connection;

// 存活令牌：Signal 首次 Connect 时创建，Signal 析构/搬走时置空 signal。
// Connection 断开前先验证令牌，源先亡则跳过对已释放对象的回调。
struct SignalAlive {
    void* signal = nullptr;
};

template<class... Args>
class Signal {
public:
    using Fn = std::function<void(Args...)>;

    Signal() = default;
    Signal(Signal&& other) noexcept
        : state_(std::move(other.state_)), alive_(std::move(other.alive_)) {
        if (alive_) alive_->signal = this;
    }
    Signal& operator=(Signal&& other) noexcept {
        if (this == &other) return *this;
        Retire();
        state_ = std::move(other.state_);
        alive_ = std::move(other.alive_);
        if (alive_) alive_->signal = this;
        return *this;
    }
    Signal(const Signal& other) {
        if (other.state_) for (auto node = other.state_->head; node; node = node->next)
            if (node->active) Subscribe(node->fn);
    }
    Signal& operator=(const Signal& other) {
        if (this == &other) return *this;
        Signal copy(other);
        *this = std::move(copy);
        return *this;
    }
    ~Signal() { Retire(); }

    Connection Connect(Fn fn);
    void Subscribe(Fn fn);
    void Disconnect(uint64_t id) noexcept;
    void Clear() noexcept {
        if (!state_) return;
        ++state_->generation;
        while (state_->head) {
            auto node = std::move(state_->head);
            state_->head = std::move(node->next);
        }
        state_->tail.reset();
    }
    // 不复制函数、不分配快照。发射中新增的槽延至下一次；退订/清空/销毁后不再调用。
    void Emit(const Args&... args) const {
        EmitState(state_, args...);
    }
    void EmitDeferred(const Args&... args) const {
        if (!state_) return;
        if (!UpdateScope::Active()) { Emit(args...); return; }
        UpdateScope::Defer(state_.get(), [weak = std::weak_ptr<State>(state_),
                                         values = std::make_tuple(args...)] {
            if (auto state = weak.lock()) std::apply([&](const auto&... value) {
                EmitState(state, value...);
            }, values);
        });
    }
    bool Empty() const noexcept { return !state_ || !state_->head; }

private:
    struct Slot;
    struct State;
    static void EmitState(std::shared_ptr<State> state, const Args&... args) {
        if (!state) return;
        const auto generation = state->generation;
        const auto end = state->next_id;
        for (auto node = state->head; node && node->id < end; node = node->next) {
            if (!state->open || state->generation != generation) break;
            if (node->active) node->fn(args...);
        }
    }
    struct Slot {
        uint64_t id;
        Fn fn;
        bool active = true;
        std::shared_ptr<Slot> next;
    };
    struct State {
        std::shared_ptr<Slot> head, tail;
        uint64_t next_id = 1, generation = 0;
        bool open = true;
        ~State() {
            while (head) { auto node = std::move(head); head = std::move(node->next); }
        }
    };
    void Retire() noexcept {
        if (alive_) alive_->signal = nullptr;
        if (state_) state_->open = false;
    }
    std::shared_ptr<State> state_;
    std::shared_ptr<SignalAlive> alive_;
};

class Connection {
public:
    Connection() noexcept = default;
    Connection(void (*erase)(void*, uint64_t), void* ctx, uint64_t id) noexcept
        : erase_(erase), signal_(ctx), id_(id) {}
    template<class... Args>
    Connection(Signal<Args...>* signal, uint64_t id,
               std::shared_ptr<SignalAlive> alive) noexcept
        : erase_([](void* p, uint64_t i) {
              static_cast<Signal<Args...>*>(p)->Disconnect(i);
          }),
          signal_(signal), id_(id), alive_(std::move(alive)) {}
    Connection(Connection&& other) noexcept { *this = std::move(other); }
    Connection& operator=(Connection&& other) noexcept {
        if (this == &other) return *this;
        Disconnect();
        erase_ = other.erase_;
        signal_ = other.signal_;
        id_ = other.id_;
        alive_ = std::move(other.alive_);
        other.erase_ = nullptr;
        other.signal_ = nullptr;
        other.id_ = 0;
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { Disconnect(); }

    void Disconnect() noexcept {
        if (erase_ && signal_ && id_) {
            // 无令牌（旧式裸构造）保持原语义；有令牌则经令牌找当前源——
            // 源已析构（置空）跳过，源被移走（指向新持有者）也能正确断开。
            if (!alive_) erase_(signal_, id_);
            else if (alive_->signal) erase_(alive_->signal, id_);
        }
        erase_ = nullptr;
        signal_ = nullptr;
        id_ = 0;
        alive_.reset();
    }
    // 放手：析构不再断开，槽随 Signal 存活。
    void Release() noexcept {
        erase_ = nullptr;
        signal_ = nullptr;
        id_ = 0;
        alive_.reset();
    }
    explicit operator bool() const noexcept {
        return signal_ != nullptr && id_ != 0 && (!alive_ || alive_->signal != nullptr);
    }

private:
    void (*erase_)(void*, uint64_t) = nullptr;
    void* signal_ = nullptr;
    uint64_t id_ = 0;
    std::shared_ptr<SignalAlive> alive_;
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
    if (!state_) state_ = std::make_shared<State>();
    if (!alive_) alive_ = std::make_shared<SignalAlive>();
    alive_->signal = this;
    auto node = std::make_shared<Slot>(Slot{state_->next_id++, std::move(fn), true, {}});
    if (state_->tail) state_->tail->next = node;
    else state_->head = node;
    state_->tail = node;
    return Connection(this, node->id, alive_);
}

template<class... Args>
void Signal<Args...>::Subscribe(Fn fn) { Connect(std::move(fn)).Release(); }

template<class... Args>
void Signal<Args...>::Disconnect(uint64_t id) noexcept {
    if (!state_) return;
    std::shared_ptr<Slot> previous;
    for (auto node = state_->head; node; node = node->next) {
        if (node->id == id) {
            node->active = false;
            if (previous) previous->next = node->next;
            else state_->head = node->next;
            if (state_->tail == node) state_->tail = previous;
            return;
        }
        previous = node;
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
        changed_.EmitDeferred(value_);
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

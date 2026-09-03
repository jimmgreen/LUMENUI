// lumen/Command.h — 可共享的命令对象：按钮 / 工具栏 / 菜单 / 快捷键共用执行与 CanExecute。
// Events: OnChanged / OnDestroyed
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Signal.h"
#include <functional>
#include <string>
#include <string_view>

namespace lumen {

class Command {
public:
    Command() = default;
    ~Command() { destroyed_.Emit(); }
    Command(std::wstring_view label, std::function<void()> execute)
        : label_(label), execute_(std::move(execute)) {}
    Command(std::wstring_view label, std::wstring_view glyph, std::wstring_view shortcut,
            std::function<void()> execute)
        : label_(label), glyph_(glyph), shortcut_(shortcut), execute_(std::move(execute)) {}

    const std::wstring& Label() const noexcept { return label_; }
    Command& Label(std::wstring_view value) {
        label_ = value;
        changed_.Emit();
        return *this;
    }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    Command& Glyph(std::wstring_view value) {
        glyph_ = value;
        changed_.Emit();
        return *this;
    }
    const std::wstring& Shortcut() const noexcept { return shortcut_; }
    Command& Shortcut(std::wstring_view value) {
        shortcut_ = value;
        changed_.Emit();
        return *this;
    }

    Command& CanExecute(std::function<bool()> fn) {
        can_execute_ = std::move(fn);
        changed_.Emit();
        return *this;
    }
    bool Enabled() const { return can_execute_ ? can_execute_() : true; }
    void RaiseCanExecuteChanged() { changed_.Emit(); }

    void Execute() {
        if (!Enabled()) return;
        if (execute_) execute_();
    }

    Connection OnChanged(std::function<void()> fn) { return changed_.Connect(std::move(fn)); }
    Connection OnDestroyed(std::function<void()> fn) { return destroyed_.Connect(std::move(fn)); }

private:
    std::wstring label_;
    std::wstring glyph_;
    std::wstring shortcut_;
    std::function<void()> execute_;
    std::function<bool()> can_execute_;
    Signal<> changed_;
    Signal<> destroyed_;
};

} // namespace lumen

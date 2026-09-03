// lumen/EmptyState.h — 空状态：圆底图标 + 标题 + 提示 + 可选主操作。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Panel.h"
#include <functional>
#include <string>

namespace lumen {

class Label;
class IconView;
class Button;

class EmptyState : public StackPanel {
public:
    EmptyState();

    EmptyState& Title(std::wstring_view value);
    const std::wstring& Title() const noexcept;
    EmptyState& Hint(std::wstring_view value);
    const std::wstring& Hint() const noexcept;
    // 顶部圆底图标（默认 kLayers）。空串隐藏。
    EmptyState& Glyph(std::wstring_view value);
    // 底部主操作按钮（默认隐藏）。
    EmptyState& Action(std::wstring_view label, std::function<void()> on_click);

protected:
    Label* title_ = nullptr;
    Label* hint_ = nullptr;
    IconView* icon_ = nullptr;
    Button* action_ = nullptr;
};

} // namespace lumen

// lumen/Menu.h — 弹出菜单。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Core.h"
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class Window;
class Control;
class Command;

// 去掉助记符 &X；&& 为字面 &。测量与绘制用去 & 后的文本。
inline void MenuParseAccess(std::wstring_view text, std::wstring& display, int& index,
                            wchar_t& key) {
    display.clear();
    index = -1;
    key = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'&' && i + 1 < text.size()) {
            if (text[i + 1] == L'&') {
                display.push_back(L'&');
                ++i;
                continue;
            }
            if (index < 0) {
                index = static_cast<int>(display.size());
                key = static_cast<wchar_t>(towupper(text[i + 1]));
            }
            continue;
        }
        display.push_back(text[i]);
    }
}

inline std::wstring MenuLabel(std::wstring_view text) {
    std::wstring display;
    int index = -1;
    wchar_t key = 0;
    MenuParseAccess(text, display, index, key);
    return display;
}

inline wchar_t MenuAccessKey(std::wstring_view text) noexcept {
    std::wstring display;
    int index = -1;
    wchar_t key = 0;
    MenuParseAccess(text, display, index, key);
    return key;
}

    struct MenuItem {
    std::wstring text;
    std::wstring shortcut;
    std::wstring glyph;      // Segoe Fluent Icons
    std::wstring radio_group;
    bool separator = false;
    bool header = false;     // 分组标题，不可点
    bool disabled = false;
    bool checked = false;
    bool checkable = false;  // 点击切换 checked
    bool radio = false;      // 同组互斥；radio_group 空则本层所有 radio 一组
    std::function<void()> action;
    std::vector<MenuItem> children;  // 非空则有子菜单
    Command* command = nullptr;

    MenuItem& Glyph(std::wstring_view value) { glyph = value; return *this; }
    MenuItem& Shortcut(std::wstring_view value) { shortcut = value; return *this; }
    MenuItem& Disabled(bool value = true) { disabled = value; return *this; }
    MenuItem& Checked(bool value = true) {
        checked = value;
        if (!radio) checkable = true;
        return *this;
    }
    MenuItem& Radio(bool value = true) {
        radio = value;
        if (value) checkable = false;
        return *this;
    }
    MenuItem& RadioGroup(std::wstring_view id) {
        radio_group = std::wstring(id);
        radio = true;
        checkable = false;
        return *this;
    }
    MenuItem& AddChild(std::wstring_view child_text, std::function<void()> child_action = {});
    static MenuItem Sub(std::wstring_view text);
    static MenuItem Header(std::wstring_view text);
};

class Menu {
public:
    MenuItem& AddItem(std::wstring_view text, std::function<void()> action);
    MenuItem& AddItem(MenuItem item);
    MenuItem& Add(Command& command);
    MenuItem& AddSeparator();
    MenuItem& AddHeader(std::wstring_view text);
    void Clear() { items_.clear(); }
    bool Empty() const noexcept { return items_.empty(); }
    const std::vector<MenuItem>& Items() const noexcept { return items_; }

    // 在窗口客户区坐标（DIP）弹出；模态运行直到关闭。
    // 返回被点击顶层项索引（分隔符不会返回），未选择返回 -1。
    // 叶子项若带 action 会在此调用；子菜单叶子同样走自身 action。
    int Popup(Window& window, Point client_point);

    // 弹到控件底边对齐（宽度不约束；屏幕工作区放不下时自动上翻）。
    // 带子项的条目悬停延迟后展开子菜单；点叶子才提交。anchor 未入窗口树时静默返回 -1。
    int PopupTo(Control& anchor);

private:
    friend class ComboBox;
    std::vector<MenuItem> items_;
};

} // namespace lumen

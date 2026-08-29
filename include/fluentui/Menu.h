// fluentui/Menu.h — 弹出菜单。
#pragma once
#include "Control.h"
#include <functional>
#include <string>
#include <vector>

namespace fui {

struct MenuItem {
    std::wstring text;
    std::wstring shortcut;
    std::wstring glyph;      // Segoe Fluent Icons
    bool separator = false;
    bool disabled = false;
    bool checked = false;
    std::function<void()> action;
};

class Menu {
public:
    MenuItem& AddItem(std::wstring_view text, std::function<void()> action);
    MenuItem& AddItem(MenuItem item);
    MenuItem& AddSeparator();
    void Clear() { items_.clear(); }

    // 在窗口客户区坐标（DIP）弹出；模态运行直到关闭。
    // 返回被点击项的索引（分隔符不会返回），未选择返回 -1。
    int Popup(Window& window, Point client_point);

private:
    friend class ComboBox;
    std::vector<MenuItem> items_;
};

} // namespace fui

// fluentui/Theme.h — Fluent 设计 token：颜色、几何、亮暗主题。
#pragma once
#include "Core.h"

namespace fui {

enum class ThemeMode { System, Light, Dark };

struct Theme {
    bool dark = false;

    // 几何 token（DIP）
    float radius_control = 4.0f;
    float radius_card = 8.0f;
    float radius_flyout = 8.0f;
    float focus_ring_width = 2.0f;
    float button_height = 32.0f;
    float input_height = 32.0f;
    float menu_item_height = 34.0f;
    float list_row_height = 30.0f;

    // 颜色 token
    Color bg;                 // 窗口背景
    Color card;               // 卡片/对话框
    Color flyout;             // 弹出层（菜单/下拉）
    Color divider;            // 分隔线
    Color text;               // 主要文本
    Color text_secondary;     // 次要文本
    Color text_disabled;      // 禁用文本
    Color control_fill;       // 控件默认底
    Color control_fill_hover;
    Color control_fill_pressed;
    Color control_stroke;     // 控件描边
    Color control_stroke_strong;
    Color accent;             // 系统强调色
    Color accent_hover;
    Color accent_pressed;
    Color accent_text;        // 强调色上的前景（自动黑/白）
    Color focus_ring;         // 键盘焦点环
    Color danger;
    Color success;
    Color selection;          // 列表选中底色
};

Theme MakeTheme(bool dark, Color accent);

Color SystemAccentColor();   // 系统强调色，取不到时回退 #0078D4
bool SystemPrefersDark();    // 系统是否偏好深色应用

} // namespace fui

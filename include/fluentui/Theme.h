// fluentui/Theme.h — Fluent 设计 token。数值对齐 WinUI / Fluent 2 视觉语言。
#pragma once
#include "Core.h"

namespace fui {

enum class ThemeMode { System, Light, Dark };

struct Theme {
    bool dark = false;

    // 几何 token（DIP）
    float radius_control = 8.0f;    // 控件圆角
    float radius_card = 8.0f;       // 卡片/对话框
    float radius_flyout = 12.0f;    // 弹出层/菜单
    float focus_ring_width = 2.0f;  // 键盘焦点环宽度（accent 色，外扩 1px）
    float button_height = 32.0f;
    float input_height = 32.0f;
    float menu_item_height = 36.0f;
    float list_row_height = 28.0f;

    // 基础色
    Color bg;                    // 窗口背景
    Color text;                  // 主要文本
    Color text_secondary;        // 次要文本
    Color text_disabled;         // 禁用文本

    // 填充层（hover/pressed 为瞬时状态切换，无过渡动画）
    Color fill_hover;            // 通用悬停层
    Color fill_pressed;          // 通用按下层
    Color fill_selected;         // 列表选中（accent 低透明）
    Color fill_input;            // 输入控件/标准按钮默认底
    Color fill_input_hover;
    Color fill_input_pressed;
    Color fill_input_focus;
    Color fill_input_disabled;

    // 描边
    Color stroke_card;           // 卡片描边
    Color stroke_divider;        // 分隔线
    Color control_stroke;        // 控件（按钮等）描边
    Color stroke_input_bottom;   // 输入框非聚焦底线
    Color edge_light;            // 按钮 1px 边缘高光（暗色在顶、亮色在底）

    // 强调色体系
    Color accent;                // 系统强调色
    Color accent_hover;          // 派生色阶
    Color accent_pressed;
    Color accent_text;           // 按感知亮度自动黑/白
    Color primary_text;          // Primary 按钮文字（暗色主题固定黑，亮色固定白）
    Color primary_text_pressed;

    Color surface_flyout;        // 菜单/弹层底
    Color scrollbar_thumb;
    Color danger;
    Color danger_hover;
    Color success;
};

Theme MakeTheme(bool dark, Color accent);

// HSV 明度/饱和度派生 accent 色阶（如暗色主题 Primary 按钮的边缘高光）。
Color ShiftAccentColor(Color accent, float value_scale, float saturation_scale);

Color SystemAccentColor();   // 系统强调色，取不到时回退 #0078D4
bool SystemPrefersDark();    // 系统是否偏好深色应用

} // namespace fui

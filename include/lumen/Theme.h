// lumen/Theme.h — LUMEN 设计 token。纯黑单色光感体系：亮白发光体压在黑面上建立层次。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Animate.h"
#include "Core.h"

namespace lumen {

enum class Elevation : uint8_t { Flat = 0, Raised = 1, Overlay = 2, Modal = 3 };

struct Theme {
    // 几何 token（DIP）
    float radius_control = 8.0f;    // 按钮/输入 rounded-lg
    float radius_card = 16.0f;      // 卡片 rounded-2xl
    float radius_flyout = 12.0f;    // 弹出层/菜单
    float focus_ring_width = 2.0f;  // 键盘焦点环宽度（白色，外扩 1px）
    float button_height = 44.0f;
    float input_height = 40.0f;
    float menu_item_height = 36.0f;
    float list_row_height = 28.0f;
    float space_sm = 4.0f;
    float space_md = 8.0f;
    float space_lg = 12.0f;
    float space_xl = 16.0f;
    float status_bar_height = 28.0f;
    float toast_duration = 2.4f;
    float tooltip_delay = 0.6f;

    // 基础色
    Color bg;                    // 窗口背景（void 黑）
    Color text;                  // 主要文本（zinc-100）
    Color text_secondary;        // 次要文本
    Color text_disabled;         // 禁用文本

    // 填充层（hover/pressed 为瞬时状态切换）
    Color fill_hover;            // 通用悬停层
    Color fill_pressed;          // 通用按下层
    Color fill_selected;         // 列表选中
    Color fill_input;            // 输入控件/标准按钮默认底（carbon）
    Color fill_input_hover;      // surface
    Color fill_input_pressed;    // surface-light
    Color fill_input_focus;
    Color fill_input_disabled;

    // 描边
    Color stroke_card;           // 卡片描边
    Color stroke_divider;        // 分隔线
    Color control_stroke;        // 控件（按钮等）静态描边
    Color stroke_input_bottom;   // 输入框非聚焦底线
    Color edge_light;            // 按钮顶部镜面高光线

    // 光感 token（除 grid_line 外均受 glow_intensity 全局缩放）
    Color glow_sm;               // 近距致密白光
    Color glow_md;               // 中距
    Color glow_lg;               // 远距大面积漫射
    Color spotlight_fill;        // 鼠标聚光内部光斑峰值（600px 圆）
    Color spotlight_border;      // 边缘折射光环峰值（400px 圆）
    Color specular_line;         // 浮层顶边 1px 镜面线
    Color ambient_flare;         // 窗口顶部环境辉光（晕影中心）
    Color grid_line;             // 背景网格线（恒定，不随强度缩放）
    float glow_intensity;        // 当前全局光效强度 0..1

    // 强调色（LUMEN 恒为纯白阶，accent 即"光"）
    Color accent;                // 纯白
    Color accent_hover;
    Color accent_pressed;
    Color accent_text;           // 白底上的文字（黑）
    Color primary_text;          // Primary 按钮文字（黑）
    Color primary_text_pressed;

    Color surface_flyout;        // 菜单/弹层底
    Color scrollbar_thumb;
    Color scrollbar_thumb_hover;
    Color danger;                // 白热警示（单色体系内最亮档）
    Color danger_hover;
    Color success;               // 降级为中灰，语义靠字形区分

    // 动效 token。motion_scale=0 表示系统关闭客户区动画（SPI_GETCLIENTAREAANIMATION）。
    float duration_fast = 0.12f;
    float duration_normal = 0.24f;
    float duration_slow = 0.40f;
    Ease ease_standard = Ease::Material;
    Ease ease_enter = Ease::CssEaseOut;
    Ease ease_exit = Ease::CssEaseIn;
    float motion_scale = 1.0f;

    // 海拔：spread 乘到 DrawGlow；glow/specular 再乘对应 alpha。
    float elevation_spread[4] = {0.0f, 0.70f, 1.15f, 1.55f};
    float elevation_glow[4] = {0.0f, 0.35f, 0.55f, 0.75f};
    float elevation_specular[4] = {0.0f, 0.35f, 0.55f, 0.80f};
};

// 子树密度 / token 覆盖。负值表示不改该项。Draw/Measure 经 Control::EffectiveTheme 合成。
enum class Density : uint8_t { Inherit, Comfortable, Normal, Compact };

struct ThemeOverride {
    Density density = Density::Inherit;
    float radius_control = -1.0f;
    float radius_card = -1.0f;
    float button_height = -1.0f;
    float input_height = -1.0f;
    float list_row_height = -1.0f;
    float space_sm = -1.0f;
    float space_md = -1.0f;
    float space_lg = -1.0f;
};

Theme ApplyThemeOverride(const Theme& base, const ThemeOverride& o) noexcept;

// glow_intensity ∈ [0,1]：缩放全部辉光/聚光 token 的 alpha。
Theme MakeTheme(float glow_intensity = 1.0f);

} // namespace lumen

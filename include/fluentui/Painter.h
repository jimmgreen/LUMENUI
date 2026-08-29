// fluentui/Painter.h — 立即模式绘制原语。所有坐标为 DIP，缩放由 BeginFrame 的变换统一处理。
#pragma once
#include "Core.h"
#include <windows.h>
#include <d2d1_3.h>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace fui {

class TextService;   // 内部类型
class LumaTextBridge;
struct Theme;
class Control;

class Painter {
public:
    Painter() = default;
    ~Painter();
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    // 每帧先调用。dc 变化时（设备重建）内部缓存自动清空。
    void BeginFrame(ID2D1DeviceContext2* dc, TextService* text, float scale);
    void EndFrame() {}

    // LumaText 渲染桥（可空 = 纯 DirectWrite）与文字底色（用于 gamma/明暗判定）。
    void SetLumaText(LumaTextBridge* luma) noexcept { luma_ = luma; }
    void SetBackdrop(Color backdrop) noexcept { backdrop_ = backdrop; }

    void FillRect(const Rect& r, Color color);
    void FillRoundedRect(const Rect& r, float radius, Color color);
    void StrokeRoundedRect(const Rect& r, float radius, Color color, float width = 1.0f);
    void DrawLine(Point a, Point b, Color color, float width = 1.0f);
    void PushClip(const Rect& r);
    void PopClip();

    // 单行文本，超出宽度自动省略号截断；垂直居中并做物理像素对齐。
    void DrawText(std::wstring_view text, const Rect& r, TextRole role, Color color,
                  Align align = Align::Leading, float max_width = 0.0f);
    Size MeasureText(std::wstring_view text, TextRole role, float max_width = 0.0f);
    // 多行换行文本；返回实际占用高度。
    float DrawTextWrapped(std::wstring_view text, const Rect& r, TextRole role, Color color);
    float MeasureTextWrapped(std::wstring_view text, TextRole role, float wrap_width);

    // Segoe Fluent Icons 字形，按指定字号在矩形内居中（始终走 DirectWrite）。
    void DrawIcon(std::wstring_view glyph, const Rect& r, float size, Color color,
                  Align align = Align::Center);

    // Fluent 焦点环：accent 色、外扩 1px、圆角 +1（宽度默认取 focus_ring_width）。
    void DrawFocusRing(const Rect& r, float radius, Color accent, float width = 2.0f);

    float Scale() const noexcept { return scale_; }

private:
    ID2D1SolidColorBrush* Brush(Color color);
    ID2D1StrokeStyle* RoundStroke();
    void DrawLayout(IDWriteTextLayout* layout, const Rect& r, Color color, Align align);

    ID2D1DeviceContext2* dc_ = nullptr;
    TextService* text_ = nullptr;
    LumaTextBridge* luma_ = nullptr;
    Color backdrop_{0.0f, 0.0f, 0.0f, 1.0f};
    float scale_ = 1.0f;
    std::unordered_map<uint32_t, ID2D1SolidColorBrush*> brushes_;
    ID2D1StrokeStyle* round_stroke_ = nullptr;
};

// 绘制一棵控件子树（按可见性递归）。供离屏渲染与测试使用。
void DrawControlTree(Painter& painter, const Theme& theme, Control* root);

} // namespace fui

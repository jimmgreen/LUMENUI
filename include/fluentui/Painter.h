// fluentui/Painter.h — 立即模式绘制原语。所有坐标为 DIP，缩放由 BeginFrame 的变换统一处理。
#pragma once
#include "Core.h"
#include <windows.h>
#include <d2d1_3.h>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace fui {

class TextService;  // 内部类型
struct Theme;
class Control;

// 控件与测试共用这组原语；画刷与文本布局全部缓存复用，每帧零分配。
class Painter {
public:
    Painter() = default;
    ~Painter();
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    // 每帧先调用。dc 变化时（设备重建）内部缓存自动清空。
    void BeginFrame(ID2D1DeviceContext2* dc, TextService* text, float scale);
    void EndFrame() {}

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

    // Segoe Fluent Icons 字形，按指定字号在矩形内居中。
    void DrawIcon(std::wstring_view glyph, const Rect& r, float size, Color color,
                  Align align = Align::Center);

    void DrawFocusRing(const Rect& r, float radius, Color color, float inset = 0.0f);

    float Scale() const noexcept { return scale_; }

private:
    ID2D1SolidColorBrush* Brush(Color color);
    ID2D1StrokeStyle* RoundStroke();
    void DrawLayout(IDWriteTextLayout* layout, const Rect& r, Color color, Align align);

    ID2D1DeviceContext2* dc_ = nullptr;
    TextService* text_ = nullptr;
    float scale_ = 1.0f;
    std::unordered_map<uint32_t, ID2D1SolidColorBrush*> brushes_;
    ID2D1StrokeStyle* round_stroke_ = nullptr;
};

// 绘制一棵控件子树（按可见性递归）。供离屏渲染与测试使用。
class Control;
void DrawControlTree(Painter& painter, const Theme& theme, Control* root);

} // namespace fui

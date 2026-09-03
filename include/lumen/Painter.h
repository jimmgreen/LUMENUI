// lumen/Painter.h — 立即模式绘制原语。所有坐标为 DIP，缩放由 BeginFrame 的变换统一处理。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
// 公共头只前向声明 D2D 接口，不拉 d2d1_3.h。windows.h 随后清掉 DrawText 宏。
#pragma once
#include "Core.h"
#include "Theme.h"
#include <windows.h>
#include "win_undef.h"
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ID2D1DeviceContext2;
struct ID2D1Bitmap;
struct ID2D1Bitmap1;
struct ID2D1BitmapBrush;
struct ID2D1SolidColorBrush;
struct ID2D1RadialGradientBrush;
struct ID2D1LinearGradientBrush;
struct ID2D1StrokeStyle;
struct ID2D1Layer;
struct ID2D1RoundedRectangleGeometry;
struct ID2D1RectangleGeometry;
struct ID2D1PathGeometry;
struct ID2D1Effect;
struct IDWriteTextLayout;

namespace lumen {

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
    // 横向三停渐变（透明→color→透明）：顶缘镜面高光等需要两端渐隐的细条。
    void FillRectHorizontalFade(const Rect& r, Color color);
    void DrawScrollThumb(const ScrollThumb& thumb, Color color);
    // dest 为 DIP（当前 BeginFrame 变换下）。NEAREST：与像素缓存 1:1 blit。
    void DrawBitmap(ID2D1Bitmap* bitmap, const Rect& dest,
                    bool smooth = false);
    ID2D1Bitmap1* CreateBitmapBgra(uint32_t width, uint32_t height, const void* pixels,
                                  uint32_t stride);
    void* DeviceIdentity() const noexcept { return dc_; }
    void FillRoundedRect(const Rect& r, float radius, Color color);
    // 垂直线性渐变填充（镜面玻璃：顶亮底暗）。画刷按颜色对缓存，每帧只突变端点。
    void FillRoundedRectLinear(const Rect& r, float radius, Color top, Color bottom);
    void StrokeRoundedRect(const Rect& r, float radius, Color color, float width = 1.0f);
    void StrokeDashedRoundedRect(const Rect& r, float radius, Color color, float width = 1.5f);
    // 外填 stroke 再内收填 fill。高对比圆角白边用双填充，避免 1px 描边覆盖不足呈锯齿。
    void FillRoundedRectFrame(const Rect& r, float radius, Color fill, Color stroke,
                              float width);
    void DrawLine(Point a, Point b, Color color, float width = 1.0f);
    // 开口折线：整段一条 path，圆头只在两端、折点 round join（禁止逐段圆帽，否则会串珠）。
    void StrokeOpenPolyline(const Point* pts, int n, Color color, float width = 1.4f,
                            bool dashed = false);
    void DrawDashedLine(Point a, Point b, Color color, float width = 1.0f);
    // 三角形填充 / 三点折线描边。几何缓存在 Painter 上，顶点未变不重建（绘制路径零堆）。
    void FillTriangle(Point a, Point b, Point c, Color color);
    void StrokePolyline(Point a, Point b, Point c, Color color, float width = 1.0f);
    // 圆弧：角度制，0° 指向右侧，正值顺时针。真 D2D 弧，圆头只在两端。
    void DrawArc(Point center, float radius, float start_degrees, float sweep_degrees,
                 Color color, float width = 2.0f);
    // antialias=false：脏区硬裁。PER_PRIMITIVE 裁剪边会把 Clear(0) 混进半像素，
    // 邻近不透明控件（Primary 按钮）上就是一条竖黑线。
    void PushClip(const Rect& r, bool antialias = true);
    void PopClip();
    int ClipDepth() const noexcept { return clip_depth_; }
    ID2D1DeviceContext2* DeviceContext() const noexcept { return dc_; }
    // 无轴对齐裁剪 / Layer / 局部变换时才允许 SetTarget 到命令列表。
    bool CanRecordCommandList() const noexcept {
        return dc_ != nullptr && clip_depth_ == 0 && !rounded_clip_active_ &&
               !rect_clip_active_ && transform_stack_.empty() && opacity_pushed_.empty();
    }
    // 圆角裁剪资源必须在控件 Prepare 阶段准备；Push/Pop 只提交绘制命令。
    void PrepareRoundedClip(const Rect& r, float radius);
    void PushRoundedClip(const Rect& r, float radius);
    void PopRoundedClip();
    // 矩形 Layer 裁剪（窗口 DIP）。必须在 PushScale 之前压栈：轴对齐裁剪不能跟非等比变换叠用。
    void PrepareRectClip(const Rect& r);
    void PushRectClip(const Rect& r);
    void PopRectClip();
    // 在当前 DPI 变换上叠加局部旋转/缩放/平移（原点为 DIP）。必须成对 PopTransform。
    // PushScale 在 |sx-1| 与 |sy-1| 都 < 1e-4 时不改矩阵（入场结束后仍走 LumaText）。
    void PushRotate(Point origin, float degrees);
    void PushScale(Point origin, float sx, float sy);
    void PushTranslate(float dx, float dy);
    void PopTransform();
    // Layer 透明度。必须成对 PopOpacity。alpha≥0.999 为短路（不压 Layer）。
    void PushOpacity(float alpha);
    void PopOpacity();

    // 单行文本，超出宽度自动省略号截断；垂直居中并做物理像素对齐。
    // 默认 LumaText；仅旋转 / 斜切 / 非等比缩放回退 DirectWrite。
    void DrawText(std::wstring_view text, const Rect& r, TextRole role, Color color,
                  Align align = Align::Leading, float max_width = 0.0f);
    Size MeasureText(std::wstring_view text, TextRole role, float max_width = 0.0f);
    // 多行换行文本；返回实际占用高度。
    float DrawTextWrapped(std::wstring_view text, const Rect& r, TextRole role, Color color,
                          Align align = Align::Leading);
    float MeasureTextWrapped(std::wstring_view text, TextRole role, float wrap_width);

    // Phosphor Regular (256 viewBox) centered; unknown glyphs fall back to Segoe Fluent Icons.
    // weight < 0 uses icon_weight_ (default 1.5: fill + extra stroke, Bold-ish).
    void DrawIcon(std::wstring_view glyph, const Rect& r, float size, Color color,
                  Align align = Align::Center, float weight = -1.f);
    // Defaults: size 16, weight icon_weight_ (1.5). Same look as control chrome.
    void DrawIcon(std::wstring_view glyph, const Rect& r, Color color,
                  Align align = Align::Center);
    void DrawIcon(std::wstring_view glyph, Point center, Color color,
                  float size = -1.f, float weight = -1.f);
    // One-off Phosphor SVG `d` (viewBox 256). `svg_path` should be a string literal
    // (geometry cached by pointer). size < 0 → 16; weight < 0 → icon_weight_.
    void DrawIconPath(const char* svg_path, const Rect& r, Color color,
                      float size = -1.f, float weight = -1.f,
                      bool filled = true, bool fatten = true);
    void SetIconWeight(float weight) noexcept { icon_weight_ = weight; }
    float IconWeight() const noexcept { return icon_weight_; }

    // 线型 Chevron（Lucide 风格）。degrees 0 朝下，正值顺时针；绕 center 原地旋转。
    void DrawChevron(Point center, float size, float degrees, Color color, float width = 1.6f);
    // 线型勾选（Lucide Check：viewBox 24 路径 (4,12)-(9,17)-(20,6)）。size 为外接正方形边长。
    void DrawCheck(Point center, float size, Color color, float width = 2.0f);

    // Fluent 焦点环：accent 色、外扩 1px、圆角 +1（宽度默认取 focus_ring_width）。
    void DrawFocusRing(const Rect& r, float radius, Color accent, float width = 2.0f);

    // —— LUMEN 光感原语 ——
    // 径向渐变填充（聚光/晕影）。center 与 r 同一坐标系（窗口 DIP）；渐变自 inner
    // 在中心衰减到 outer 于 light_radius*inner_stop 处，之外保持 outer。
    // 渐变画刷按（颜色对 + 量化 inner_stop）缓存，每帧只做 SetCenter/SetRadius 突变。
    void FillRectRadial(const Rect& r, Point center, float light_radius, Color inner,
                        Color outer, float inner_stop = 0.45f);
    void FillRoundedRectRadial(const Rect& r, float radius, Point center, float light_radius,
                               Color inner, Color outer, float inner_stop = 0.45f);
    // 径向渐变描边圆角矩形：渐变只落在 1px 环带上，等效 CSS mask-composite 边框折射光。
    void StrokeRoundedRectRadial(const Rect& r, float radius, Point center, float light_radius,
                                 Color inner, Color outer, float inner_stop = 0.45f,
                                 float width = 1.0f);
    // 外发光：沿圆角矩形法线均匀衰减（边线性 + 角径向），等效 CSS
    // box-shadow 0 0 20px。spread 1 ≈ 20 DIP，1.75 ≈ hover 35px。
    // wrap_corners：不透明底上包住圆角耳朵（两/三参默认 true）。透明 HWND（菜单）必须 false，否则盒角露白。
    void DrawGlow(const Rect& r, float radius, Color glow);
    void DrawGlow(const Rect& r, float radius, Color glow, float spread);
    void DrawGlow(const Rect& r, float radius, Color glow, float spread, bool wrap_corners);
    // 顶部/底部内凹高光：圆角填充裁一条贴边窄带（等效 inset 0 1px），不描边上半圈。
    void DrawInnerLight(const Rect& r, float radius, Color specular, Color shade);
    // 流光边框：亮带沿 angle（弧度）方向扫过描边，随时间旋转即 shimmer。
    void StrokeRoundedRectSweep(const Rect& r, float radius, float angle, Color hot, Color base,
                                float width = 1.0f);
    // 文字辉光：同布局低透明 8 向偏移晕染后画主 pass，仅用于标题类少量文本。
    void DrawTextGlow(std::wstring_view text, const Rect& r, TextRole role, Color color,
                      Align align = Align::Leading, float max_width = 0.0f);

    float Scale() const noexcept { return scale_; }

    // 蓝噪声平铺：压在渐变/辉光上打碎 8bit 色带。绘制路径零堆。
    void OverlayDither(const Rect& r);

    // 模态遮罩 Acrylic：CopyFrom 当前帧，高斯模糊 + 去饱和后缓存。
    // 只在下层变化时 Capture；常态帧 DrawAcrylic 只 blit 缓存（sigma 变才重栅格）。
    bool CaptureAcrylic();
    void InvalidateAcrylic();
    bool HasAcrylic() const noexcept;
    void DrawAcrylic(const Rect& r, float sigma, float dim);

private:
    ID2D1SolidColorBrush* Brush(Color color);
    ID2D1StrokeStyle* RoundStroke();
    ID2D1StrokeStyle* DashStroke();
    void DrawLayout(IDWriteTextLayout* layout, const Rect& r, Color color, Align align);
    // 渐变画刷缓存：键 = 颜色对 + 量化衰减档；每帧仅突变 center/radius/端点。
    ID2D1RadialGradientBrush* RadialBrush(Color inner, Color outer, float inner_stop);
    // 圆角外发光角：0..hold 实心，之后与 LinearGlowBrush 同一套衰减。
    ID2D1RadialGradientBrush* RadialBrushHalo(Color glow, float hold);
    ID2D1LinearGradientBrush* LinearGradientBrush(Color a, Color b, Color c);
    ID2D1LinearGradientBrush* LinearGlowBrush(Color glow);
    void ReleaseBrushes();
    void EnsureDither();
    struct CachedPath {
        Point a{};
        Point b{};
        Point c{};
        bool closed = false;
        ID2D1PathGeometry* geometry = nullptr;
    };
    ID2D1PathGeometry* EnsurePath(CachedPath& slot, Point a, Point b, Point c, bool closed);
    ID2D1PathGeometry* EnsureIconGeometry(const char* d);
    void PaintPhosphor(const char* d, bool fill, bool fatten, const Rect& r, float size,
                       Color color, Align align, float weight);

    ID2D1DeviceContext2* dc_ = nullptr;
    TextService* text_ = nullptr;
    LumaTextBridge* luma_ = nullptr;
    Color backdrop_{0.0f, 0.0f, 0.0f, 1.0f};
    float scale_ = 1.0f;
    float icon_weight_ = 1.5f;
    std::unordered_map<uint32_t, ID2D1SolidColorBrush*> brushes_;
    std::unordered_map<uint64_t, ID2D1RadialGradientBrush*> radial_brushes_;
    std::unordered_map<uint64_t, ID2D1LinearGradientBrush*> linear_brushes_;
    ID2D1StrokeStyle* round_stroke_ = nullptr;
    ID2D1StrokeStyle* dash_stroke_ = nullptr;
    ID2D1Layer* rounded_clip_layer_ = nullptr;
    ID2D1Layer* rect_clip_layer_ = nullptr;
    std::vector<ID2D1Layer*> opacity_layers_;
    std::vector<uint8_t> opacity_pushed_;   // 1 = 真正 PushLayer 了
    int opacity_layer_top_ = 0;
    int clip_depth_ = 0;
    struct RoundedClip {
        Rect rect;
        float radius = 0.0f;
        ID2D1RoundedRectangleGeometry* geometry = nullptr;
    };
    std::vector<RoundedClip> rounded_clips_;
    struct RectClip {
        Rect rect;
        ID2D1RectangleGeometry* geometry = nullptr;
    };
    std::vector<RectClip> rect_clips_;
    CachedPath fill_triangle_;
    CachedPath stroke_polyline_;
    ID2D1PathGeometry* arc_geometry_ = nullptr;
    ID2D1PathGeometry* poly_geometry_ = nullptr;
    std::unordered_map<const char*, ID2D1PathGeometry*> icon_geometries_;
    bool rounded_clip_active_ = false;
    bool rect_clip_active_ = false;
    struct Affine2x3 {
        float m11 = 1.0f, m12 = 0.0f, m21 = 0.0f, m22 = 1.0f, dx = 0.0f, dy = 0.0f;
    };
    std::vector<Affine2x3> transform_stack_;
    ID2D1Bitmap* dither_bitmap_ = nullptr;
    ID2D1BitmapBrush* dither_brush_ = nullptr;
    ID2D1Bitmap1* acrylic_scene_ = nullptr;
    ID2D1Bitmap1* acrylic_blurred_ = nullptr;
    ID2D1Effect* acrylic_blur_ = nullptr;
    ID2D1Effect* acrylic_sat_ = nullptr;
    bool acrylic_captured_ = false;
    bool acrylic_output_dirty_ = true;
    float acrylic_sigma_px_ = -1.0f;
    uint32_t acrylic_w_ = 0;
    uint32_t acrylic_h_ = 0;

    void ReleaseAcrylic();
    bool EnsureAcrylicBitmaps(uint32_t w, uint32_t h);
    bool EnsureAcrylicEffects();
    void RasterizeAcrylic(float sigma_px);
};

// 海拔套件：外发光 + 填充 + 顶缘镜面 + 描边。fill.a=0 时用 theme.fill_input。
// wrap_corners 默认 true（主窗口不透明底）；菜单等透明 HWND 传 false。
void DrawElevated(Painter& painter, const Theme& theme, const Rect& r, float radius,
                  Elevation elevation, Color fill = {}, bool wrap_corners = true);

// 与 DrawText 同一套单行测量：优先 LumaText 步进宽 + 墨迹外扩；无桥时回退 DirectWrite。
Size MeasureUiText(std::wstring_view text, TextRole role, float max_width = 0.0f,
                   LumaTextBridge* luma = nullptr);

// 步进宽（不含墨迹外扩）。助记键下划线必须走这个，否则会宽出半个字。
float AdvanceUiText(std::wstring_view text, TextRole role, LumaTextBridge* luma = nullptr);

// 在 display[index] 下画 1px 下划线，位置与 DrawText 对齐。
void DrawMnemonicUnderline(Painter& painter, std::wstring_view display, int index, const Rect& r,
                           TextRole role, Color color, Align align, LumaTextBridge* luma);

// 与 DrawTextWrapped 相同的换行算法，返回多行总高度（布局测量用）。优先 LumaText。
float MeasureWrappedHeight(std::wstring_view text_view, TextRole role, float width,
                           LumaTextBridge* luma = nullptr);

// 聚光卡片标准光效：内部光斑（600px）+ 边缘折射光环（400px）+ 静态描边。
// center 为窗口 DIP 坐标（SpotlightCenter：卡片内为绝对 + mouse_local_），intensity 0..1。
// 画在控件填充之后、内容之前。
void DrawSpotlight(Painter& painter, const Theme& theme, const Rect& r, float radius,
                   Point center, float intensity);

// 绘制一棵控件子树（按可见性递归）。供离屏渲染与测试使用。
void DrawControlTree(Painter& painter, const Theme& theme, Control* root);
void DrawControlTree(Painter& painter, const Theme& theme, Control* root, const Rect& clip);

} // namespace lumen

#include "win_undef.h"

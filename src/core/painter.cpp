#include "lumen/Painter.h"
#include "lumen/Icons.h"
#include "icon_path.h"
#include "lumen/Theme.h"
#include "lumatext_bridge.h"
#include "text_service.h"
#include <d2d1_3.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include "lumen/win_undef.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lumen {
namespace {

// d2d1effects.h 的 CLSID 只声明不定义；静态库存 GUID 避免 INITGUID 污染其它 TU。
constexpr CLSID kGaussianBlurClsid{0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
constexpr CLSID kSaturationClsid{0x5cb2d9cf, 0x327d, 0x459f, {0xa0, 0xce, 0x40, 0xc0, 0xb2, 0x08, 0x6b, 0xf7}};

D2D1_COLOR_F ToD2D(Color c) { return {c.r, c.g, c.b, c.a}; }
D2D1_RECT_F ToD2D(const Rect& r) { return {r.x, r.y, r.Right(), r.Bottom()}; }

uint32_t PackColor(Color c) {
    auto q = [](float v) {
        uint32_t x = static_cast<uint32_t>(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        return x > 255 ? 255u : x;
    };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

float Snap(float value, float scale) {
    return std::floor(value * scale + 0.5f) / scale;
}

DWRITE_TEXT_ALIGNMENT MapAlign(Align align) {
    switch (align) {
    case Align::Center: return DWRITE_TEXT_ALIGNMENT_CENTER;
    case Align::Trailing: return DWRITE_TEXT_ALIGNMENT_TRAILING;
    default: return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

// LumaText 按物理像素栅格化，必须 1:1 上屏。轴对齐均匀缩放（含 DPI、Dialog/Toast
// 入场）把 DIP 映到像素并把 m._11 当 scale；旋转 / 斜切 / 非等比才回退 DirectWrite。
bool AxisUniformScale(const D2D1_MATRIX_3X2_F& m, float& s) noexcept {
    if (std::fabs(m._12) > 1.0e-4f || std::fabs(m._21) > 1.0e-4f) return false;
    if (!(m._11 > 0.05f) || !(m._22 > 0.05f)) return false;
    const float mag = std::max(m._11, m._22);
    if (std::fabs(m._11 - m._22) > 0.02f * mag) return false;
    s = m._11;
    return true;
}

Color LumaBackdropHint(Color ink, Color backdrop) noexcept {
    const float text_lum = 0.2126f * ink.r + 0.7152f * ink.g + 0.0722f * ink.b;
    const float bg_lum = 0.2126f * backdrop.r + 0.7152f * backdrop.g + 0.0722f * backdrop.b;
    if (std::fabs(text_lum - bg_lum) >= 0.25f) return backdrop;
    return text_lum < 0.5f ? Color{1.0f, 1.0f, 1.0f, 1.0f} : Color{0.0f, 0.0f, 0.0f, 1.0f};
}

bool TryLumaDraw(ID2D1DeviceContext2* dc, LumaTextBridge* luma, std::wstring_view text,
                 IDWriteTextFormat* format, float dip_x, float dip_y, float dip_w, float dip_h,
                 Align align, Color color, Color backdrop) {
    if (!dc || !luma || !format || dip_w <= 0.0f || dip_h <= 0.0f) return false;
    D2D1_MATRIX_3X2_F xf;
    dc->GetTransform(&xf);
    float luma_scale = 1.0f;
    if (!AxisUniformScale(xf, luma_scale)) return false;
    const float x0 = dip_x * xf._11 + xf._31;
    const float y0 = dip_y * xf._22 + xf._32;
    const float x1 = (dip_x + dip_w) * xf._11 + xf._31;
    const float y1 = (dip_y + dip_h) * xf._22 + xf._32;
    const float ink = 2.0f + format->GetFontSize() * luma_scale * 0.15f;
    float left = x0;
    float right = x1;
    if (align == Align::Trailing) {
        left -= ink;
    } else if (align == Align::Center) {
        left -= ink;
        right += ink;
    } else {
        right += ink;
    }
    const float top = std::floor(y0 + 0.5f);
    const float bottom = std::max(std::floor(y1 + 0.5f), top + 1.0f);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    const bool drawn =
        luma->Draw(text, format, D2D1_RECT_F{left, top, right, bottom}, ToD2D(color),
                   ToD2D(LumaBackdropHint(color, backdrop)), luma_scale, MapAlign(align));
    dc->SetTransform(xf);
    return drawn;
}

bool MostlyCjk(std::wstring_view text) noexcept {
    int cjk = 0;
    int other = 0;
    for (wchar_t ch : text) {
        if (ch <= 0x20) continue;
        if ((ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xF900 && ch <= 0xFAFF) ||
            (ch >= 0xFF00 && ch <= 0xFFEF)) {
            ++cjk;
        } else {
            ++other;
        }
    }
    return cjk > 0 && cjk >= other;
}

D2D1_COLOR_F GlowStop(Color glow, float a) { return {glow.r, glow.g, glow.b, glow.a * a}; }

} // namespace

// 换行只用步进宽。MeasureUiText 的墨迹外扩是整行一次，按字累加会提前折行。
float AdvanceUiText(std::wstring_view text, TextRole role, LumaTextBridge* luma) {
    if (text.empty()) return 0.0f;
    IDWriteTextFormat* format = UiText().Format(role);
    if (luma && luma->Enabled()) {
        float width = 0.0f;
        if (luma->Measure(text, format, width, nullptr)) return width;
    }
    return UiText().MeasureText(text, role, 0.0f).w;
}

Size MeasureUiText(std::wstring_view text, TextRole role, float max_width, LumaTextBridge* luma) {
    if (text.empty()) return {};
    IDWriteTextFormat* format = UiText().Format(role);
    if (luma && luma->Enabled()) {
        float width = 0.0f;
        float height = 0.0f;
        if (luma->Measure(text, format, width, &height)) {
            const float font = format ? format->GetFontSize() : 14.0f;
            const float pad = 2.0f + font * 0.15f;
            if (max_width > 0.0f) width = std::min(width, max_width);
            return {width + pad, height > 0.5f ? height : font * 1.25f};
        }
    }
    return UiText().MeasureText(text, role, max_width);
}

Painter::~Painter() { ReleaseBrushes(); }

void Painter::ReleaseBrushes() {
    for (auto& entry : brushes_) entry.second->Release();
    brushes_.clear();
    for (auto& entry : radial_brushes_) entry.second->Release();
    radial_brushes_.clear();
    for (auto& entry : linear_brushes_) entry.second->Release();
    linear_brushes_.clear();
    if (round_stroke_) {
        round_stroke_->Release();
        round_stroke_ = nullptr;
    }
    if (dash_stroke_) {
        dash_stroke_->Release();
        dash_stroke_ = nullptr;
    }
    if (rounded_clip_layer_) {
        rounded_clip_layer_->Release();
        rounded_clip_layer_ = nullptr;
    }
    if (rect_clip_layer_) {
        rect_clip_layer_->Release();
        rect_clip_layer_ = nullptr;
    }
    for (ID2D1Layer* layer : opacity_layers_) {
        if (layer) layer->Release();
    }
    opacity_layers_.clear();
    opacity_pushed_.clear();
    for (RoundedClip& clip : rounded_clips_) {
        if (clip.geometry) clip.geometry->Release();
    }
    rounded_clips_.clear();
    rounded_clip_active_ = false;
    for (RectClip& clip : rect_clips_) {
        if (clip.geometry) clip.geometry->Release();
    }
    rect_clips_.clear();
    rect_clip_active_ = false;
    auto release_path = [](CachedPath& slot) {
        if (slot.geometry) {
            slot.geometry->Release();
            slot.geometry = nullptr;
        }
        slot.a = slot.b = slot.c = {};
        slot.closed = false;
    };
    release_path(fill_triangle_);
    release_path(stroke_polyline_);
    if (arc_geometry_) {
        arc_geometry_->Release();
        arc_geometry_ = nullptr;
    }
    if (poly_geometry_) {
        poly_geometry_->Release();
        poly_geometry_ = nullptr;
    }
    if (dither_brush_) {
        dither_brush_->Release();
        dither_brush_ = nullptr;
    }
    if (dither_bitmap_) {
        dither_bitmap_->Release();
        dither_bitmap_ = nullptr;
    }
    ReleaseAcrylic();
    for (auto& entry : icon_geometries_) {
        if (entry.second) entry.second->Release();
    }
    icon_geometries_.clear();
}

void Painter::BeginFrame(ID2D1DeviceContext2* dc, TextService* text, float scale) {
    if (dc != dc_) {
        ReleaseBrushes();
        dc_ = dc;
    }
    text_ = text;
    scale_ = scale;
    transform_stack_.clear();
    opacity_pushed_.clear();
    opacity_layer_top_ = 0;
    clip_depth_ = 0;
    if (!dc_) return;
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // 预乘合成表面禁用 ClearType（半透明卡片上会出彩边），灰度 + 自定义 gamma。
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (text_) {
        if (IDWriteRenderingParams* params = text_->GrayscaleParams()) {
            dc_->SetTextRenderingParams(params);
        }
    }
}

void Painter::EnsureDither() {
    if (dither_brush_ || !dc_) return;
    constexpr UINT kN = 64;
    uint8_t pixels[kN * kN * 4];
    for (UINT y = 0; y < kN; ++y) {
        for (UINT x = 0; x < kN; ++x) {
            float f = std::fmod(52.9829189f * std::fmod(0.06711056f * static_cast<float>(x) +
                                                            0.00583715f * static_cast<float>(y),
                                                        1.0f),
                                1.0f);
            if (f < 0.0f) f += 1.0f;
            const uint8_t t = static_cast<uint8_t>(f * 3.0f);   // 0..2 / 255
            const size_t i = (static_cast<size_t>(y) * kN + x) * 4u;
            pixels[i + 0] = t;
            pixels[i + 1] = t;
            pixels[i + 2] = t;
            pixels[i + 3] = t;
        }
    }
    dither_bitmap_ = CreateBitmapBgra(kN, kN, pixels, kN * 4u);
    if (!dither_bitmap_) return;
    ID2D1BitmapBrush* brush = nullptr;
    if (FAILED(dc_->CreateBitmapBrush(dither_bitmap_, &brush)) || !brush) {
        dither_bitmap_->Release();
        dither_bitmap_ = nullptr;
        return;
    }
    brush->SetExtendModeX(D2D1_EXTEND_MODE_WRAP);
    brush->SetExtendModeY(D2D1_EXTEND_MODE_WRAP);
    brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    dither_brush_ = brush;
}

void Painter::OverlayDither(const Rect& r) {
    if (!dc_ || r.IsEmpty()) return;
    EnsureDither();
    if (!dither_brush_) return;
    const float s = 1.0f / std::max(scale_, 0.01f);
    dither_brush_->SetTransform(D2D1::Matrix3x2F::Scale(s, s));
    dc_->FillRectangle(ToD2D(r), dither_brush_);
}

void Painter::ReleaseAcrylic() {
    if (acrylic_blur_) {
        acrylic_blur_->Release();
        acrylic_blur_ = nullptr;
    }
    if (acrylic_sat_) {
        acrylic_sat_->Release();
        acrylic_sat_ = nullptr;
    }
    if (acrylic_blurred_) {
        acrylic_blurred_->Release();
        acrylic_blurred_ = nullptr;
    }
    if (acrylic_scene_) {
        acrylic_scene_->Release();
        acrylic_scene_ = nullptr;
    }
    acrylic_captured_ = false;
    acrylic_output_dirty_ = true;
    acrylic_sigma_px_ = -1.0f;
    acrylic_w_ = 0;
    acrylic_h_ = 0;
}

void Painter::InvalidateAcrylic() {
    acrylic_captured_ = false;
    acrylic_output_dirty_ = true;
    acrylic_sigma_px_ = -1.0f;
}

bool Painter::HasAcrylic() const noexcept {
    if (!acrylic_captured_ || !acrylic_scene_ || !dc_) return false;
    const D2D1_SIZE_U px = dc_->GetPixelSize();
    return acrylic_w_ == px.width && acrylic_h_ == px.height && px.width > 0 && px.height > 0;
}

bool Painter::EnsureAcrylicBitmaps(uint32_t w, uint32_t h) {
    if (!dc_ || w == 0 || h == 0) return false;
    if (acrylic_scene_ && acrylic_blurred_ && acrylic_w_ == w && acrylic_h_ == h) return true;
    if (acrylic_scene_) {
        acrylic_scene_->Release();
        acrylic_scene_ = nullptr;
    }
    if (acrylic_blurred_) {
        acrylic_blurred_->Release();
        acrylic_blurred_ = nullptr;
    }
    acrylic_captured_ = false;
    acrylic_output_dirty_ = true;
    acrylic_sigma_px_ = -1.0f;
    float dpi_x = 96.0f, dpi_y = 96.0f;
    dc_->GetDpi(&dpi_x, &dpi_y);
    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    props.dpiX = dpi_x;
    props.dpiY = dpi_y;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    const D2D1_SIZE_U size{w, h};
    if (FAILED(dc_->CreateBitmap(size, nullptr, 0, &props, &acrylic_scene_)) || !acrylic_scene_) {
        acrylic_scene_ = nullptr;
        return false;
    }
    if (FAILED(dc_->CreateBitmap(size, nullptr, 0, &props, &acrylic_blurred_)) ||
        !acrylic_blurred_) {
        acrylic_scene_->Release();
        acrylic_scene_ = nullptr;
        acrylic_blurred_ = nullptr;
        return false;
    }
    acrylic_w_ = w;
    acrylic_h_ = h;
    return true;
}

bool Painter::EnsureAcrylicEffects() {
    if (!dc_) return false;
    if (acrylic_blur_ && acrylic_sat_) return true;
    if (acrylic_blur_) {
        acrylic_blur_->Release();
        acrylic_blur_ = nullptr;
    }
    if (acrylic_sat_) {
        acrylic_sat_->Release();
        acrylic_sat_ = nullptr;
    }
    if (FAILED(dc_->CreateEffect(kGaussianBlurClsid, &acrylic_blur_)) || !acrylic_blur_) {
        acrylic_blur_ = nullptr;
        return false;
    }
    acrylic_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
                            D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
    acrylic_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    if (FAILED(dc_->CreateEffect(kSaturationClsid, &acrylic_sat_)) || !acrylic_sat_) {
        acrylic_blur_->Release();
        acrylic_blur_ = nullptr;
        acrylic_sat_ = nullptr;
        return false;
    }
    acrylic_sat_->SetValue(D2D1_SATURATION_PROP_SATURATION, 0.20f);
    return true;
}

void Painter::RasterizeAcrylic(float sigma_px) {
    if (!dc_ || !acrylic_scene_ || !acrylic_blurred_ || !EnsureAcrylicEffects()) return;
    acrylic_blur_->SetInput(0, acrylic_scene_);
    acrylic_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma_px);
    acrylic_sat_->SetInputEffect(0, acrylic_blur_);
    ID2D1Image* previous = nullptr;
    dc_->GetTarget(&previous);
    D2D1_MATRIX_3X2_F old{};
    dc_->GetTransform(&old);
    dc_->SetTarget(acrylic_blurred_);
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    dc_->DrawImage(acrylic_sat_);
    dc_->SetTransform(old);
    dc_->SetTarget(previous);
    if (previous) previous->Release();
    acrylic_sigma_px_ = sigma_px;
    acrylic_output_dirty_ = false;
}

bool Painter::CaptureAcrylic() {
    if (!dc_) return false;
    const D2D1_SIZE_U px = dc_->GetPixelSize();
    if (!EnsureAcrylicBitmaps(px.width, px.height)) {
        acrylic_captured_ = false;
        return false;
    }
    const HRESULT hr = acrylic_scene_->CopyFromRenderTarget(nullptr, dc_, nullptr);
    acrylic_captured_ = SUCCEEDED(hr);
    acrylic_output_dirty_ = true;
    acrylic_sigma_px_ = -1.0f;
    return acrylic_captured_;
}

void Painter::DrawAcrylic(const Rect& r, float sigma, float dim) {
    if (!dc_ || r.IsEmpty()) return;
    if (!HasAcrylic()) {
        if (dim > 0.001f) FillRect(r, Color{0.0f, 0.0f, 0.0f, dim});
        return;
    }
    const float sigma_px = std::max(0.0f, sigma) * std::max(scale_, 0.01f);
    if (sigma_px < 0.20f) {
        DrawBitmap(acrylic_scene_, r, false);
    } else {
        if (acrylic_output_dirty_ || std::fabs(sigma_px - acrylic_sigma_px_) > 0.05f) {
            RasterizeAcrylic(sigma_px);
        }
        if (acrylic_blurred_ && !acrylic_output_dirty_) DrawBitmap(acrylic_blurred_, r, false);
        else DrawBitmap(acrylic_scene_, r, false);
    }
    OverlayDither(r);
    if (dim > 0.001f) FillRect(r, Color{0.0f, 0.0f, 0.0f, dim});
}

ID2D1SolidColorBrush* Painter::Brush(Color color) {
    const uint32_t key = PackColor(color);
    auto it = brushes_.find(key);
    if (it != brushes_.end()) return it->second;
    if (brushes_.size() >= 320) {
        for (auto& entry : brushes_) entry.second->Release();
        brushes_.clear();
    }
    ID2D1SolidColorBrush* brush = nullptr;
    if (FAILED(dc_->CreateSolidColorBrush(ToD2D(color), &brush))) return nullptr;
    brushes_.emplace(key, brush);
    return brush;
}

ID2D1RadialGradientBrush* Painter::RadialBrush(Color inner, Color outer, float inner_stop) {
    const float stop = Clamp(inner_stop, 0.1f, 1.0f);
    // 键位布局：衰减档 0-3 位、outer RGB 4-27 位、inner 全色 28-59 位，无重叠不碰撞。
    const uint64_t key =
        (static_cast<uint64_t>(PackColor(inner)) << 28) |
        (static_cast<uint64_t>(PackColor(outer) & 0x00FFFFFFu) << 4) |
        static_cast<uint64_t>(std::min(static_cast<uint32_t>(stop * 16.0f), 15u));
    auto it = radial_brushes_.find(key);
    if (it != radial_brushes_.end()) return it->second;
    if (radial_brushes_.size() >= 64) {
        for (auto& entry : radial_brushes_) entry.second->Release();
        radial_brushes_.clear();
    }
    ID2D1GradientStopCollection* stops = nullptr;
    auto mix = [&](float t) {
        t = Clamp(t, 0.0f, 1.0f);
        const float s = t * t * (3.0f - 2.0f * t);
        return Color{Lerp(inner.r, outer.r, s), Lerp(inner.g, outer.g, s),
                     Lerp(inner.b, outer.b, s), Lerp(inner.a, outer.a, s)};
    };
    const D2D1_GRADIENT_STOP ramp[7] = {
        {0.0f, ToD2D(inner)},
        {stop * 0.12f, ToD2D(mix(0.08f))},
        {stop * 0.28f, ToD2D(mix(0.22f))},
        {stop * 0.48f, ToD2D(mix(0.45f))},
        {stop * 0.68f, ToD2D(mix(0.70f))},
        {stop * 0.86f, ToD2D(mix(0.90f))},
        {stop, ToD2D(outer)}};
    if (FAILED(dc_->CreateGradientStopCollection(ramp, 7, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP,
                                                 &stops))) {
        return nullptr;
    }
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{};
    props.radiusX = props.radiusY = 1.0f;
    ID2D1RadialGradientBrush* brush = nullptr;
    const bool ok = SUCCEEDED(dc_->CreateRadialGradientBrush(props, stops, &brush));
    stops->Release();
    if (!ok) return nullptr;
    radial_brushes_.emplace(key, brush);
    return brush;
}

ID2D1RadialGradientBrush* Painter::RadialBrushHalo(Color glow, float hold) {
    hold = Clamp(hold, 0.05f, 0.90f);
    const Color fade{glow.r, glow.g, glow.b, 0.0f};
    const float span = 1.0f - hold;
    const uint64_t key = (1ull << 63) | (static_cast<uint64_t>(PackColor(glow)) << 8) |
                         static_cast<uint64_t>(std::min(static_cast<uint32_t>(hold * 32.0f), 31u));
    auto it = radial_brushes_.find(key);
    if (it != radial_brushes_.end()) return it->second;
    if (radial_brushes_.size() >= 64) {
        for (auto& entry : radial_brushes_) entry.second->Release();
        radial_brushes_.clear();
    }
    ID2D1GradientStopCollection* stops = nullptr;
    // ???????? RGB ????hold ?????????premultiplied?????????
    const float pre_hold = std::max(0.0f, hold - 0.04f);
    const D2D1_GRADIENT_STOP ring[7] = {
        {0.0f, ToD2D(fade)},
        {pre_hold, ToD2D(fade)},
        {hold, GlowStop(glow, 1.00f)},
        {hold + 0.15f * span, GlowStop(glow, 0.55f)},
        {hold + 0.35f * span, GlowStop(glow, 0.25f)},
        {hold + 0.65f * span, GlowStop(glow, 0.08f)},
        {1.0f, ToD2D(fade)}};
    if (FAILED(dc_->CreateGradientStopCollection(ring, 7, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP,
                                                 &stops))) {
        return nullptr;
    }
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{};
    props.radiusX = props.radiusY = 1.0f;
    ID2D1RadialGradientBrush* brush = nullptr;
    const bool ok = SUCCEEDED(dc_->CreateRadialGradientBrush(props, stops, &brush));
    stops->Release();
    if (!ok) return nullptr;
    radial_brushes_.emplace(key, brush);
    return brush;
}

ID2D1LinearGradientBrush* Painter::LinearGlowBrush(Color glow) {
    const Color fade{glow.r, glow.g, glow.b, 0.0f};
    const uint64_t key = (1ull << 63) | PackColor(glow);
    auto it = linear_brushes_.find(key);
    if (it != linear_brushes_.end()) return it->second;
    if (linear_brushes_.size() >= 64) {
        for (auto& entry : linear_brushes_) entry.second->Release();
        linear_brushes_.clear();
    }
    ID2D1GradientStopCollection* stops = nullptr;
    const D2D1_GRADIENT_STOP ramp[5] = {{0.00f, GlowStop(glow, 1.00f)},
                                        {0.15f, GlowStop(glow, 0.55f)},
                                        {0.35f, GlowStop(glow, 0.25f)},
                                        {0.65f, GlowStop(glow, 0.08f)},
                                        {1.00f, ToD2D(fade)}};
    if (FAILED(dc_->CreateGradientStopCollection(ramp, 5, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP,
                                                 &stops))) {
        return nullptr;
    }
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    ID2D1LinearGradientBrush* brush = nullptr;
    const bool ok = SUCCEEDED(dc_->CreateLinearGradientBrush(props, stops, &brush));
    stops->Release();
    if (!ok) return nullptr;
    linear_brushes_.emplace(key, brush);
    return brush;
}

ID2D1LinearGradientBrush* Painter::LinearGradientBrush(Color a, Color b, Color c) {
    uint64_t key = PackColor(a);
    key = (key * 0x9E3779B97F4A7C15ull) ^ PackColor(b);
    key = (key * 0x9E3779B97F4A7C15ull) ^ PackColor(c);
    auto it = linear_brushes_.find(key);
    if (it != linear_brushes_.end()) return it->second;
    if (linear_brushes_.size() >= 64) {
        for (auto& entry : linear_brushes_) entry.second->Release();
        linear_brushes_.clear();
    }
    ID2D1GradientStopCollection* stops = nullptr;
    const D2D1_GRADIENT_STOP triple[3] = {{0.0f, ToD2D(a)}, {0.8f, ToD2D(b)}, {1.0f, ToD2D(c)}};
    if (FAILED(dc_->CreateGradientStopCollection(triple, 3, &stops))) return nullptr;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    ID2D1LinearGradientBrush* brush = nullptr;
    const bool ok = SUCCEEDED(dc_->CreateLinearGradientBrush(props, stops, &brush));
    stops->Release();
    if (!ok) return nullptr;
    linear_brushes_.emplace(key, brush);
    return brush;
}

ID2D1StrokeStyle* Painter::RoundStroke() {
    if (round_stroke_) return round_stroke_;
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return nullptr;
    D2D1_STROKE_STYLE_PROPERTIES props{};
    props.startCap = D2D1_CAP_STYLE_ROUND;
    props.endCap = D2D1_CAP_STYLE_ROUND;
    props.dashCap = D2D1_CAP_STYLE_ROUND;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    props.miterLimit = 10.0f;
    props.dashStyle = D2D1_DASH_STYLE_SOLID;
    factory->CreateStrokeStyle(props, nullptr, 0, &round_stroke_);
    factory->Release();
    return round_stroke_;
}

ID2D1StrokeStyle* Painter::DashStroke() {
    if (dash_stroke_) return dash_stroke_;
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return nullptr;
    D2D1_STROKE_STYLE_PROPERTIES props{};
    props.startCap = D2D1_CAP_STYLE_ROUND;
    props.endCap = D2D1_CAP_STYLE_ROUND;
    props.dashCap = D2D1_CAP_STYLE_ROUND;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    props.miterLimit = 10.0f;
    props.dashStyle = D2D1_DASH_STYLE_CUSTOM;
    const FLOAT dashes[] = {4.0f, 3.0f};
    factory->CreateStrokeStyle(props, dashes, 2, &dash_stroke_);
    factory->Release();
    return dash_stroke_;
}

void Painter::FillRect(const Rect& r, Color color) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    dc_->FillRectangle(ToD2D(r), Brush(color));
}

void Painter::FillRectHorizontalFade(const Rect& r, Color color) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    const Color fade{color.r, color.g, color.b, 0.0f};
    ID2D1LinearGradientBrush* brush = LinearGradientBrush(fade, color, fade);
    if (!brush) return;
    brush->SetStartPoint(D2D1::Point2F(r.x, r.y));
    brush->SetEndPoint(D2D1::Point2F(r.Right(), r.y));
    dc_->FillRectangle(ToD2D(r), brush);
}

void Painter::DrawScrollThumb(const ScrollThumb& thumb, Color color) {
    if (!thumb.visible || thumb.rect.IsEmpty()) return;
    const float radius = (thumb.rect.w < thumb.rect.h ? thumb.rect.w : thumb.rect.h) * 0.5f;
    FillRoundedRect(thumb.rect, radius, color);
}

void Painter::DrawBitmap(ID2D1Bitmap* bitmap, const Rect& dest, bool smooth) {
    if (!dc_ || !bitmap || dest.IsEmpty()) return;
    const D2D1_RECT_F dest_rect = ToD2D(dest);
    dc_->DrawBitmap(bitmap, dest_rect, 1.0f,
                    smooth ? D2D1_INTERPOLATION_MODE_LINEAR
                           : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

ID2D1Bitmap1* Painter::CreateBitmapBgra(uint32_t width, uint32_t height, const void* pixels,
                                        uint32_t stride) {
    if (!dc_ || !pixels || width == 0 || height == 0 || stride < width * 4u) return nullptr;
    ID2D1Bitmap1* bitmap = nullptr;
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU(width, height), pixels, stride, props, &bitmap))) {
        return nullptr;
    }
    return bitmap;
}

void Painter::FillRoundedRect(const Rect& r, float radius, Color color) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    dc_->FillRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(r), radius, radius}, Brush(color));
}

void Painter::FillRoundedRectLinear(const Rect& r, float radius, Color top, Color bottom) {
    if (!dc_ || r.IsEmpty() || (top.a <= 0.0f && bottom.a <= 0.0f)) return;
    // 三停靠点落在 0 / 0.8 / 1；中点取 80% 插值后即为线性渐变。
    const Color mid{Lerp(top.r, bottom.r, 0.8f), Lerp(top.g, bottom.g, 0.8f),
                    Lerp(top.b, bottom.b, 0.8f), Lerp(top.a, bottom.a, 0.8f)};
    ID2D1LinearGradientBrush* brush = LinearGradientBrush(top, mid, bottom);
    if (!brush) return;
    brush->SetStartPoint(D2D1::Point2F(r.x + r.w * 0.5f, r.y));
    brush->SetEndPoint(D2D1::Point2F(r.x + r.w * 0.5f, r.Bottom()));
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    dc_->FillRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(r), radius, radius}, brush);
}

void Painter::StrokeRoundedRect(const Rect& r, float radius, Color color, float width) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    // 描边向内收半个线宽做像素对齐，填充不收。
    const Rect inset = r.Inset(width * 0.5f, width * 0.5f);
    radius = std::max(0.0f, radius - width * 0.5f);
    dc_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(inset), radius, radius}, Brush(color),
                              width, RoundStroke());
}

void Painter::StrokeDashedRoundedRect(const Rect& r, float radius, Color color, float width) {
    if (!dc_ || r.IsEmpty() || color.a <= 0.0f) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    const Rect inset = r.Inset(width * 0.5f, width * 0.5f);
    radius = std::max(0.0f, radius - width * 0.5f);
    dc_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(inset), radius, radius}, Brush(color),
                              width, DashStroke());
}

void Painter::FillRoundedRectFrame(const Rect& r, float radius, Color fill, Color stroke,
                                    float width) {
    if (!dc_ || r.IsEmpty()) return;
    if (stroke.a <= 0.0f || width <= 0.0f) {
        FillRoundedRect(r, radius, fill);
        return;
    }
    FillRoundedRect(r, radius, stroke);
    const Rect inner = r.Inset(width, width);
    if (inner.IsEmpty() || fill.a <= 0.0f) return;
    FillRoundedRect(inner, std::max(0.0f, radius - width), fill);
}

void Painter::DrawLine(Point a, Point b, Color color, float width) {
    if (!dc_ || color.a <= 0.0f) return;
    dc_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), Brush(color), width,
                  RoundStroke());
}

void Painter::DrawDashedLine(Point a, Point b, Color color, float width) {
    if (!dc_ || color.a <= 0.0f) return;
    dc_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), Brush(color), width,
                  DashStroke());
}

void Painter::StrokeOpenPolyline(const Point* pts, int n, Color color, float width, bool dashed) {
    if (!dc_ || !pts || n < 2 || color.a <= 0.0f || width <= 0.0f) return;
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return;
    if (poly_geometry_) {
        poly_geometry_->Release();
        poly_geometry_ = nullptr;
    }
    if (FAILED(factory->CreatePathGeometry(&poly_geometry_)) || !poly_geometry_) {
        factory->Release();
        return;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(poly_geometry_->Open(&sink)) || !sink) {
        poly_geometry_->Release();
        poly_geometry_ = nullptr;
        factory->Release();
        return;
    }
    sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i < n; ++i) {
        sink->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    const HRESULT hr = sink->Close();
    sink->Release();
    factory->Release();
    if (FAILED(hr)) {
        poly_geometry_->Release();
        poly_geometry_ = nullptr;
        return;
    }
    dc_->DrawGeometry(poly_geometry_, Brush(color), width,
                      dashed ? DashStroke() : RoundStroke());
}

ID2D1PathGeometry* Painter::EnsurePath(CachedPath& slot, Point a, Point b, Point c, bool closed) {
    if (slot.geometry && slot.closed == closed && slot.a.x == a.x && slot.a.y == a.y &&
        slot.b.x == b.x && slot.b.y == b.y && slot.c.x == c.x && slot.c.y == c.y) {
        return slot.geometry;
    }
    if (slot.geometry) {
        slot.geometry->Release();
        slot.geometry = nullptr;
    }
    slot.a = a;
    slot.b = b;
    slot.c = c;
    slot.closed = closed;
    if (!dc_) return nullptr;
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return nullptr;
    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geometry)) || !geometry) {
        factory->Release();
        return nullptr;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink) {
        geometry->Release();
        factory->Release();
        return nullptr;
    }
    sink->BeginFigure(D2D1::Point2F(a.x, a.y),
                      closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(b.x, b.y));
    sink->AddLine(D2D1::Point2F(c.x, c.y));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    const HRESULT hr = sink->Close();
    sink->Release();
    factory->Release();
    if (FAILED(hr)) {
        geometry->Release();
        return nullptr;
    }
    slot.geometry = geometry;
    return geometry;
}

ID2D1PathGeometry* Painter::EnsureIconGeometry(const char* d) {
    if (!d || !dc_) return nullptr;
    auto it = icon_geometries_.find(d);
    if (it != icon_geometries_.end()) return it->second;
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return nullptr;
    ID2D1PathGeometry* geometry = nullptr;
    const HRESULT hr = BuildSvgPath(factory, d, &geometry);
    factory->Release();
    if (FAILED(hr) || !geometry) return nullptr;
    icon_geometries_.emplace(d, geometry);
    return geometry;
}


void Painter::FillTriangle(Point a, Point b, Point c, Color color) {
    if (!dc_ || color.a <= 0.0f) return;
    ID2D1PathGeometry* geometry = EnsurePath(fill_triangle_, a, b, c, true);
    if (!geometry) return;
    dc_->FillGeometry(geometry, Brush(color));
}

void Painter::StrokePolyline(Point a, Point b, Point c, Color color, float width) {
    if (!dc_ || color.a <= 0.0f || width <= 0.0f) return;
    ID2D1PathGeometry* geometry = EnsurePath(stroke_polyline_, a, b, c, false);
    if (!geometry) return;
    dc_->DrawGeometry(geometry, Brush(color), width, RoundStroke());
}

void Painter::DrawArc(Point center, float radius, float start_degrees, float sweep_degrees,
                      Color color, float width) {
    if (!dc_ || color.a <= 0.0f || std::fabs(sweep_degrees) < 0.01f || radius <= 0.0f) return;
    const float sweep = Clamp(sweep_degrees, -359.9f, 359.9f);
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return;
    if (arc_geometry_) {
        arc_geometry_->Release();
        arc_geometry_ = nullptr;
    }
    if (FAILED(factory->CreatePathGeometry(&arc_geometry_)) || !arc_geometry_) {
        factory->Release();
        return;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(arc_geometry_->Open(&sink)) || !sink) {
        arc_geometry_->Release();
        arc_geometry_ = nullptr;
        factory->Release();
        return;
    }
    constexpr float kRad = 3.14159265f / 180.0f;
    const float a0 = start_degrees * kRad;
    const float a1 = (start_degrees + sweep) * kRad;
    const D2D1_POINT_2F p0{center.x + radius * std::cos(a0), center.y + radius * std::sin(a0)};
    const D2D1_POINT_2F p1{center.x + radius * std::cos(a1), center.y + radius * std::sin(a1)};
    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point = p1;
    arc.size = D2D1::SizeF(radius, radius);
    arc.rotationAngle = 0.0f;
    arc.sweepDirection = sweep >= 0.0f ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                       : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
    arc.arcSize = std::fabs(sweep) > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    const HRESULT hr = sink->Close();
    sink->Release();
    factory->Release();
    if (FAILED(hr)) {
        arc_geometry_->Release();
        arc_geometry_ = nullptr;
        return;
    }
    dc_->DrawGeometry(arc_geometry_, Brush(color), width, RoundStroke());
}

void Painter::PushClip(const Rect& r, bool antialias) {
    if (!dc_) return;
    dc_->PushAxisAlignedClip(ToD2D(r), antialias ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
                                                 : D2D1_ANTIALIAS_MODE_ALIASED);
    ++clip_depth_;
}

void Painter::PopClip() {
    if (!dc_) return;
    dc_->PopAxisAlignedClip();
    if (clip_depth_ > 0) --clip_depth_;
}

void Painter::PrepareRoundedClip(const Rect& r, float radius) {
    if (!dc_ || r.IsEmpty()) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    for (const RoundedClip& clip : rounded_clips_) {
        if (clip.radius == radius && clip.rect.x == r.x && clip.rect.y == r.y &&
            clip.rect.w == r.w && clip.rect.h == r.h) {
            return;
        }
    }
    if (!rounded_clip_layer_ && FAILED(dc_->CreateLayer(&rounded_clip_layer_))) return;

    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return;
    ID2D1RoundedRectangleGeometry* geometry = nullptr;
    const HRESULT result = factory->CreateRoundedRectangleGeometry(
        D2D1_ROUNDED_RECT{ToD2D(r), radius, radius}, &geometry);
    factory->Release();
    if (FAILED(result) || !geometry) return;
    constexpr size_t kMaxRoundedClips = 64;
    if (rounded_clips_.size() >= kMaxRoundedClips) {
        rounded_clips_.front().geometry->Release();
        rounded_clips_.erase(rounded_clips_.begin());
    }
    rounded_clips_.push_back({r, radius, geometry});
}

void Painter::PushRoundedClip(const Rect& r, float radius) {
    if (!dc_ || r.IsEmpty() || rounded_clip_active_ || !rounded_clip_layer_) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    ID2D1RoundedRectangleGeometry* geometry = nullptr;
    for (const RoundedClip& clip : rounded_clips_) {
        if (clip.radius == radius && clip.rect.x == r.x && clip.rect.y == r.y &&
            clip.rect.w == r.w && clip.rect.h == r.h) {
            geometry = clip.geometry;
            break;
        }
    }
    if (!geometry) return;
    const D2D1_LAYER_PARAMETERS1 parameters = D2D1::LayerParameters1(
        D2D1::InfiniteRect(), geometry, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS1_NONE);
    dc_->PushLayer(parameters, rounded_clip_layer_);
    rounded_clip_active_ = true;
}

void Painter::PopRoundedClip() {
    if (!dc_ || !rounded_clip_active_) return;
    dc_->PopLayer();
    rounded_clip_active_ = false;
}

void Painter::PrepareRectClip(const Rect& r) {
    if (!dc_ || r.IsEmpty()) return;
    for (const RectClip& clip : rect_clips_) {
        if (clip.rect.x == r.x && clip.rect.y == r.y && clip.rect.w == r.w &&
            clip.rect.h == r.h) {
            return;
        }
    }
    if (!rect_clip_layer_ && FAILED(dc_->CreateLayer(&rect_clip_layer_))) return;
    ID2D1Factory* factory = nullptr;
    dc_->GetFactory(&factory);
    if (!factory) return;
    ID2D1RectangleGeometry* geometry = nullptr;
    const HRESULT result = factory->CreateRectangleGeometry(ToD2D(r), &geometry);
    factory->Release();
    if (FAILED(result) || !geometry) return;
    constexpr size_t kMaxRectClips = 64;
    if (rect_clips_.size() >= kMaxRectClips) {
        rect_clips_.front().geometry->Release();
        rect_clips_.erase(rect_clips_.begin());
    }
    rect_clips_.push_back({r, geometry});
}

void Painter::PushRectClip(const Rect& r) {
    if (!dc_ || r.IsEmpty() || rect_clip_active_ || !rect_clip_layer_) return;
    ID2D1RectangleGeometry* geometry = nullptr;
    for (const RectClip& clip : rect_clips_) {
        if (clip.rect.x == r.x && clip.rect.y == r.y && clip.rect.w == r.w &&
            clip.rect.h == r.h) {
            geometry = clip.geometry;
            break;
        }
    }
    if (!geometry) return;
    const D2D1_LAYER_PARAMETERS1 parameters = D2D1::LayerParameters1(
        D2D1::InfiniteRect(), geometry, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS1_NONE);
    dc_->PushLayer(parameters, rect_clip_layer_);
    rect_clip_active_ = true;
}

void Painter::PopRectClip() {
    if (!dc_ || !rect_clip_active_) return;
    dc_->PopLayer();
    rect_clip_active_ = false;
}

void Painter::PushRotate(Point origin, float degrees) {
    if (!dc_) return;
    D2D1::Matrix3x2F cur;
    dc_->GetTransform(&cur);
    transform_stack_.push_back({cur._11, cur._12, cur._21, cur._22, cur._31, cur._32});
    const D2D1::Matrix3x2F rot =
        D2D1::Matrix3x2F::Rotation(degrees, D2D1::Point2F(origin.x, origin.y));
    D2D1::Matrix3x2F next;
    next.SetProduct(rot, cur);
    dc_->SetTransform(next);
}

void Painter::PushScale(Point origin, float sx, float sy) {
    if (!dc_) return;
    D2D1::Matrix3x2F cur;
    dc_->GetTransform(&cur);
    transform_stack_.push_back({cur._11, cur._12, cur._21, cur._22, cur._31, cur._32});
    if (std::fabs(sx - 1.0f) < 1.0e-4f && std::fabs(sy - 1.0f) < 1.0e-4f) return;
    const D2D1::Matrix3x2F sc =
        D2D1::Matrix3x2F::Scale(sx, sy, D2D1::Point2F(origin.x, origin.y));
    D2D1::Matrix3x2F next;
    next.SetProduct(sc, cur);
    dc_->SetTransform(next);
}

void Painter::PushTranslate(float dx, float dy) {
    if (!dc_) return;
    D2D1::Matrix3x2F cur;
    dc_->GetTransform(&cur);
    transform_stack_.push_back({cur._11, cur._12, cur._21, cur._22, cur._31, cur._32});
    const D2D1::Matrix3x2F tr = D2D1::Matrix3x2F::Translation(dx, dy);
    D2D1::Matrix3x2F next;
    next.SetProduct(tr, cur);
    dc_->SetTransform(next);
}

void Painter::PopTransform() {
    if (!dc_ || transform_stack_.empty()) return;
    const Affine2x3& a = transform_stack_.back();
    dc_->SetTransform(D2D1::Matrix3x2F(a.m11, a.m12, a.m21, a.m22, a.dx, a.dy));
    transform_stack_.pop_back();
}

void Painter::PushOpacity(float alpha) {
    if (!dc_) return;
    alpha = Clamp(alpha, 0.0f, 1.0f);
    const bool apply = alpha < 0.999f;
    opacity_pushed_.push_back(apply ? 1 : 0);
    if (!apply) return;
    ID2D1Layer* layer = nullptr;
    if (static_cast<int>(opacity_layers_.size()) <= opacity_layer_top_) {
        if (FAILED(dc_->CreateLayer(&layer)) || !layer) {
            opacity_pushed_.back() = 0;
            return;
        }
        opacity_layers_.push_back(layer);
    } else {
        layer = opacity_layers_[static_cast<size_t>(opacity_layer_top_)];
    }
    const D2D1_LAYER_PARAMETERS1 parameters = D2D1::LayerParameters1(
        D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), alpha, nullptr, D2D1_LAYER_OPTIONS1_NONE);
    dc_->PushLayer(parameters, layer);
    ++opacity_layer_top_;
}

void Painter::PopOpacity() {
    if (!dc_ || opacity_pushed_.empty()) return;
    const bool apply = opacity_pushed_.back() != 0;
    opacity_pushed_.pop_back();
    if (!apply) return;
    dc_->PopLayer();
    if (opacity_layer_top_ > 0) --opacity_layer_top_;
}

void Painter::DrawLayout(IDWriteTextLayout* layout, const Rect& r, Color color, Align align) {
    if (!layout) return;
    (void)align;   // layout 已按 align 排版（LineLayout→SetTextAlignment），再补偿 x 会双重偏移
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float y = Snap(r.y + (r.h - metrics.height) * 0.5f, scale_);
    dc_->DrawTextLayout(D2D1::Point2F(r.x, y), layout, Brush(color),
                         D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Painter::DrawText(std::wstring_view text, const Rect& r, TextRole role, Color color,
                       Align align, float max_width) {
    if (!dc_ || !text_ || r.IsEmpty() || text.empty()) return;
    float width = max_width > 0.0f ? max_width : r.w;
    IDWriteTextFormat* format = text_->Format(role);
    if (!format) return;
    // 行盒上下取整到物理像素，保证基线稳定（与 DirectWrite 回退路径一致）。
    Rect snapped = r;
    snapped.y = Snap(r.y, scale_);
    snapped.h = std::max(Snap(r.Bottom(), scale_) - snapped.y, 1.0f / scale_);
    // 混排含 emoji：正文走 YaHei/Segoe 灰度，彩色 run 由 LumaText 内部回退 DirectWrite。
    // 墨迹外扩加在非对齐侧：步进宽略小于栅格外扩时不要走省略号，且不能推动 Trailing 边。
    if (TryLumaDraw(dc_, luma_, text, format, snapped.x, snapped.y, width, snapped.h, align, color,
                    backdrop_)) {
        return;
    }
    IDWriteTextLayout* layout = text_->LineLayout(text, format, width, align);
    if (!layout) return;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float y = Snap(snapped.y + (snapped.h - metrics.height) * 0.5f, scale_);
    const float nudge = MostlyCjk(text) ? text_->CjkBaselineNudge(format->GetFontSize()) : 0.0f;
    dc_->DrawTextLayout(D2D1::Point2F(snapped.x, y - nudge), layout, Brush(color),
                        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

Size Painter::MeasureText(std::wstring_view text, TextRole role, float max_width) {
    return MeasureUiText(text, role, max_width, luma_);
}

namespace {

bool CanBreakAfter(wchar_t ch) noexcept {
    if (ch == L' ') return true;
    // CJK 统一表意区与全角区允许在字符后断行
    return (ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xFF00 && ch <= 0xFFEF);
}

// 在 [begin, end) 内按宽度软换行（不含硬换行符），逐行交给 sink——绘制路径零堆分配。
template <typename Sink>
void SoftWrapRange(std::wstring_view text_view, size_t begin, size_t end, TextRole role,
                   float width, LumaTextBridge* luma, Sink&& sink) {
    if (begin >= end) {
        sink(text_view.substr(begin, 0));
        return;
    }
    size_t line_start = begin;
    size_t last_break = std::wstring_view::npos;
    float x = 0.0f;
    for (size_t i = begin; i < end; ++i) {
        const float char_w = AdvanceUiText(text_view.substr(i, 1), role, luma);
        if (x + char_w > width && i > line_start) {
            if (last_break != std::wstring_view::npos && last_break > line_start) {
                sink(text_view.substr(line_start, last_break - line_start));
                line_start = last_break;
            } else {
                sink(text_view.substr(line_start, i - line_start));
                line_start = i;
            }
            while (line_start < end && text_view[line_start] == L' ') ++line_start;
            x = 0.0f;
            last_break = std::wstring_view::npos;
            i = line_start - 1;
            continue;
        }
        x += char_w;
        if (CanBreakAfter(text_view[i])) {
            last_break = text_view[i] == L' ' ? i : i + 1;
        }
    }
    if (line_start < end) sink(text_view.substr(line_start, end - line_start));
    else if (line_start == end && (end == begin || text_view[end - 1] == L'\n')) {
        sink(text_view.substr(end, 0));
    }
}

// 先按 \n 硬断行，再逐段软换行。
template <typename Sink>
void EachWrappedLine(std::wstring_view text_view, TextRole role, float width, LumaTextBridge* luma,
                     Sink&& sink) {
    if (text_view.empty()) {
        sink(text_view);
        return;
    }
    size_t para = 0;
    for (size_t i = 0; i <= text_view.size(); ++i) {
        if (i == text_view.size() || text_view[i] == L'\n') {
            SoftWrapRange(text_view, para, i, role, width, luma, sink);
            para = i + 1;
        }
    }
}

} // namespace

float Painter::DrawTextWrapped(std::wstring_view text, const Rect& r, TextRole role, Color color,
                               Align align) {
    if (!dc_ || !text_ || r.IsEmpty() || text.empty()) return 0.0f;
    const float line_h = MeasureUiText(L"m4B", role, 0.0f, luma_).h;
    if (!(line_h > 0.0f)) return 0.0f;
    float y = r.y;
    EachWrappedLine(text, role, r.w, luma_, [&](std::wstring_view line) {
        DrawText(line, {r.x, y, r.w, line_h}, role, color, align);
        y += line_h;
    });
    return y - r.y;
}

float Painter::MeasureTextWrapped(std::wstring_view text, TextRole role, float wrap_width) {
    return MeasureWrappedHeight(text, role, wrap_width, luma_);
}

void Painter::PaintPhosphor(const char* d, bool fill, bool fatten, const Rect& r, float size,
                            Color color, Align align, float weight) {
    if (!dc_ || !d || r.IsEmpty() || color.a <= 0.0f || size <= 0.0f) return;
    const float resolved = weight < 0.0f ? icon_weight_ : weight;
    const float extra_vb = Clamp((resolved - 1.0f) * 16.0f, 0.0f, 20.0f);
    ID2D1PathGeometry* geometry = EnsureIconGeometry(d);
    if (!geometry) return;
    const float cx = align == Align::Leading ? r.x + size * 0.5f
                     : align == Align::Trailing ? r.Right() - size * 0.5f
                                                : r.x + r.w * 0.5f;
    const float cy = r.y + r.h * 0.5f;
    const float origin_x = cx - size * 0.5f;
    const float origin_y = cy - size * 0.5f;
    const float s = size / 256.0f;
    D2D1_MATRIX_3X2_F saved;
    dc_->GetTransform(&saved);
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                      D2D1::Matrix3x2F::Translation(origin_x, origin_y) * saved);
    ID2D1SolidColorBrush* brush = Brush(color);
    if (brush) {
        if (fill) {
            dc_->FillGeometry(geometry, brush);
            // Closed Regular outline is 16vb; extra stroke fattens toward Bold (weight 1.5 = +8vb).
            if (extra_vb > 0.05f && fatten) {
                dc_->DrawGeometry(geometry, brush, extra_vb, RoundStroke());
            }
        } else {
            const float stroke_dip = std::max(1.2f, size * (16.0f / 256.0f));
            const float stroke_vb = (s > 0.0f ? stroke_dip / s : 16.0f) + extra_vb;
            dc_->DrawGeometry(geometry, brush, stroke_vb, RoundStroke());
        }
    }
    dc_->SetTransform(saved);
}

void Painter::DrawIcon(std::wstring_view glyph, const Rect& r, Color color, Align align) {
    DrawIcon(glyph, r, icon::kSize, color, align);
}

void Painter::DrawIcon(std::wstring_view glyph, Point center, Color color, float size, float weight) {
    const float s = size < 0.0f ? icon::kSize : size;
    DrawIcon(glyph, {center.x - s * 0.5f, center.y - s * 0.5f, s, s}, s, color, Align::Center,
             weight);
}

void Painter::DrawIconPath(const char* svg_path, const Rect& r, Color color, float size,
                           float weight, bool filled, bool fatten) {
    const float s = size < 0.0f ? icon::kSize : size;
    PaintPhosphor(svg_path, filled, fatten, r, s, color, Align::Center, weight);
}

void Painter::DrawIcon(std::wstring_view glyph, const Rect& r, float size, Color color,
                       Align align, float weight) {
    if (!dc_ || r.IsEmpty() || glyph.empty() || color.a <= 0.0f || size <= 0.0f) return;
    const PhosphorIcon* spec = FindPhosphorIcon(glyph.front());
    if (spec && spec->d) {
        PaintPhosphor(spec->d, spec->fill, spec->fatten, r, size, color, align, weight);
        return;
    }
    if (!text_) return;
    IDWriteTextFormat* format = text_->IconFormat(size);
    if (!format) return;
    if (TryLumaDraw(dc_, luma_, glyph, format, r.x, r.y, r.w, r.h, align, color, backdrop_)) {
        return;
    }
    IDWriteTextLayout* layout = text_->LineLayout(glyph, format, 1.0e5f, Align::Leading);
    if (!layout) return;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float x = align == Align::Center ? r.x + (r.w - metrics.width) * 0.5f
                                     : (align == Align::Trailing ? r.Right() - metrics.width : r.x);
    float y = r.y + (r.h - metrics.height) * 0.5f;
    dc_->DrawTextLayout(D2D1::Point2F(Snap(x, scale_), Snap(y, scale_)), layout, Brush(color),
                        D2D1_DRAW_TEXT_OPTIONS_NONE);
}


void Painter::DrawChevron(Point center, float size, float degrees, Color color, float width) {
    if (!dc_ || color.a <= 0.0f || size <= 0.0f) return;
    // Lucide chevron-down：viewBox 24 路径 (6,9)-(12,15)-(18,9)，绕几何中心原地转。
    const float rad = degrees * 3.14159265f / 180.0f;
    const float c = std::cos(rad), s = std::sin(rad);
    const float w = size * 0.25f;
    const float h = size * 0.125f;
    auto map = [&](float x, float y) -> Point {
        return {center.x + x * c - y * s, center.y + x * s + y * c};
    };
    DrawLine(map(-w, -h), map(0.0f, h), color, width);
    DrawLine(map(0.0f, h), map(w, -h), color, width);
}

void Painter::DrawCheck(Point center, float size, Color color, float width) {
    if (!dc_ || color.a <= 0.0f || size <= 0.0f) return;
    const float s = size / 24.0f;
    auto map = [&](float x, float y) -> Point {
        return {center.x + (x - 12.0f) * s, center.y + (y - 11.5f) * s};
    };
    DrawLine(map(4.0f, 12.0f), map(9.0f, 17.0f), color, width);
    DrawLine(map(9.0f, 17.0f), map(20.0f, 6.0f), color, width);
}

void Painter::DrawFocusRing(const Rect& r, float radius, Color accent, float width) {
    if (!dc_ || accent.a <= 0.0f) return;
    const Rect ring = r.Inset(-1.0f, -1.0f);
    StrokeRoundedRect(ring, radius + 1.0f, accent, width);
}

void Painter::FillRectRadial(const Rect& r, Point center, float light_radius, Color inner,
                             Color outer, float inner_stop) {
    if (!dc_ || r.IsEmpty() || (inner.a <= 0.0f && outer.a <= 0.0f)) return;
    ID2D1RadialGradientBrush* brush = RadialBrush(inner, outer, inner_stop);
    if (!brush) return;
    brush->SetCenter(D2D1::Point2F(center.x, center.y));
    brush->SetRadiusX(light_radius);
    brush->SetRadiusY(light_radius);
    dc_->FillRectangle(ToD2D(r), brush);
    OverlayDither(r);
}

void Painter::FillRoundedRectRadial(const Rect& r, float radius, Point center, float light_radius,
                                    Color inner, Color outer, float inner_stop) {
    if (!dc_ || r.IsEmpty() || (inner.a <= 0.0f && outer.a <= 0.0f)) return;
    ID2D1RadialGradientBrush* brush = RadialBrush(inner, outer, inner_stop);
    if (!brush) return;
    brush->SetCenter(D2D1::Point2F(center.x, center.y));
    brush->SetRadiusX(light_radius);
    brush->SetRadiusY(light_radius);
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    dc_->FillRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(r), radius, radius}, brush);
    OverlayDither(r);
}

void Painter::StrokeRoundedRectRadial(const Rect& r, float radius, Point center,
                                      float light_radius, Color inner, Color outer,
                                      float inner_stop, float width) {
    if (!dc_ || r.IsEmpty() || (inner.a <= 0.0f && outer.a <= 0.0f)) return;
    ID2D1RadialGradientBrush* brush = RadialBrush(inner, outer, inner_stop);
    if (!brush) return;
    brush->SetCenter(D2D1::Point2F(center.x, center.y));
    brush->SetRadiusX(light_radius);
    brush->SetRadiusY(light_radius);
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    const Rect inset = r.Inset(width * 0.5f, width * 0.5f);
    radius = std::max(0.0f, radius - width * 0.5f);
    dc_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(inset), radius, radius}, brush, width,
                              RoundStroke());
}

void Painter::DrawGlow(const Rect& r, float radius, Color glow) {
    DrawGlow(r, radius, glow, 1.0f, true);
}

void Painter::DrawGlow(const Rect& r, float radius, Color glow, float spread) {
    DrawGlow(r, radius, glow, spread, true);
}

void Painter::DrawGlow(const Rect& r, float radius, Color glow, float spread, bool wrap_corners) {
    if (!dc_ || r.IsEmpty() || glow.a <= 0.0f) return;
    spread = std::max(0.25f, spread);
    const float short_side = std::min(r.w, r.h);
    const float rad = std::min(std::max(radius, 0.0f), short_side * 0.5f);
    // Capsule/circle: AABB corners sit ~0.41*rad outside the arc. The old halo-sized
    // corner quads painted those ears (inside the box, outside the pill). Keep the
    // falloff inside that gap so hover does not flash a rounded rectangle.
    const bool stadium = rad >= short_side * 0.45f;
    float blur = 20.0f * spread;
    if (stadium) blur = std::min(blur, rad * 0.32f);
    const float halo = rad + blur;
    const float hold = rad / std::max(halo, 0.001f);
    ID2D1LinearGradientBrush* linear = LinearGlowBrush(glow);
    ID2D1RadialGradientBrush* radial = RadialBrushHalo(glow, hold);
    if (!linear || !radial) return;

    auto strip = [&](const Rect& band, Point start, Point end) {
        if (band.w <= 0.0f || band.h <= 0.0f) return;
        linear->SetStartPoint(D2D1::Point2F(start.x, start.y));
        linear->SetEndPoint(D2D1::Point2F(end.x, end.y));
        dc_->FillRectangle(ToD2D(band), linear);
    };
    auto corner = [&](const Rect& box, Point center) {
        if (box.w <= 0.0f || box.h <= 0.0f) return;
        radial->SetCenter(D2D1::Point2F(center.x, center.y));
        radial->SetRadiusX(halo);
        radial->SetRadiusY(halo);
        dc_->FillRectangle(ToD2D(box), radial);
    };

    const float straight_w = r.w - rad * 2.0f;
    const float straight_h = r.h - rad * 2.0f;
    if (straight_w > 0.25f) {
        strip({r.x + rad, r.y - blur, straight_w, blur},
              {r.x + r.w * 0.5f, r.y}, {r.x + r.w * 0.5f, r.y - blur});
        strip({r.x + rad, r.Bottom(), straight_w, blur},
              {r.x + r.w * 0.5f, r.Bottom()}, {r.x + r.w * 0.5f, r.Bottom() + blur});
    }
    if (straight_h > 0.25f) {
        strip({r.x - blur, r.y + rad, blur, straight_h},
              {r.x, r.y + r.h * 0.5f}, {r.x - blur, r.y + r.h * 0.5f});
        strip({r.Right(), r.y + rad, blur, straight_h},
              {r.Right(), r.y + r.h * 0.5f}, {r.Right() + blur, r.y + r.h * 0.5f});
    }
    if (stadium) {
        if (r.w >= r.h) {
            const float cy = r.y + r.h * 0.5f;
            if (wrap_corners) {
                corner({r.x - blur, r.y - blur, rad + blur, r.h + 2.0f * blur}, {r.x + rad, cy});
                corner({r.Right() - rad, r.y - blur, rad + blur, r.h + 2.0f * blur},
                       {r.Right() - rad, cy});
            } else {
                corner({r.x - blur, r.y - blur, blur, r.h + 2.0f * blur}, {r.x + rad, cy});
                corner({r.Right(), r.y - blur, blur, r.h + 2.0f * blur}, {r.Right() - rad, cy});
            }
        } else {
            const float cx = r.x + r.w * 0.5f;
            if (wrap_corners) {
                corner({r.x - blur, r.y - blur, r.w + 2.0f * blur, rad + blur}, {cx, r.y + rad});
                corner({r.x - blur, r.Bottom() - rad, r.w + 2.0f * blur, rad + blur},
                       {cx, r.Bottom() - rad});
            } else {
                corner({r.x - blur, r.y - blur, r.w + 2.0f * blur, blur}, {cx, r.y + rad});
                corner({r.x - blur, r.Bottom(), r.w + 2.0f * blur, blur}, {cx, r.Bottom() - rad});
            }
        }
    } else if (wrap_corners) {
        // 不透明底：角扇盖住圆角外、盒内的耳朵，白底键才不会露黑三角。
        const float cx0 = r.x + rad;
        const float cy0 = r.y + rad;
        const float cx1 = r.Right() - rad;
        const float cy1 = r.Bottom() - rad;
        const float cap = rad + blur;
        corner({r.x - blur, r.y - blur, cap, cap}, {cx0, cy0});
        corner({cx1, r.y - blur, cap, cap}, {cx1, cy0});
        corner({r.x - blur, cy1, cap, cap}, {cx0, cy1});
        corner({cx1, cy1, cap, cap}, {cx1, cy1});
    } else {
        // 透明 HWND：只画 AABB 外侧。角扇铺进盒内会在预乘合成上露出方白角。
        const float cx0 = r.x + rad;
        const float cy0 = r.y + rad;
        const float cx1 = r.Right() - rad;
        const float cy1 = r.Bottom() - rad;
        corner({r.x - blur, r.y - blur, blur, halo}, {cx0, cy0});
        corner({r.x, r.y - blur, rad, blur}, {cx0, cy0});
        corner({r.Right(), r.y - blur, blur, halo}, {cx1, cy0});
        corner({r.Right() - rad, r.y - blur, rad, blur}, {cx1, cy0});
        corner({r.x - blur, r.Bottom() - rad, blur, halo}, {cx0, cy1});
        corner({r.x - blur, r.Bottom(), halo, blur}, {cx0, cy1});
        corner({r.Right(), r.Bottom() - rad, blur, halo}, {cx1, cy1});
        corner({r.Right() - rad, r.Bottom(), halo, blur}, {cx1, cy1});
    }
    OverlayDither({r.x - blur, r.y - blur, r.w + blur * 2.0f, r.h + blur * 2.0f});
}

void Painter::DrawInnerLight(const Rect& r, float radius, Color specular, Color shade) {
    if (!dc_ || r.IsEmpty()) return;
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    const Rect inner = r.Inset(1.0f, 1.0f);
    const float rr = std::max(0.0f, radius - 1.0f);
    // 填充整颗圆角再裁 1.5px 顶/底：高光是贴顶的月牙，不是沿轮廓描边。
    const float band = 1.5f;
    const float clip_h = 1.0f + band;
    PushClip({r.x, r.y, r.w, clip_h});
    FillRoundedRect(inner, rr, specular);
    PopClip();
    PushClip({r.x, r.Bottom() - clip_h, r.w, clip_h});
    FillRoundedRect(inner, rr, shade);
    PopClip();
}

void Painter::StrokeRoundedRectSweep(const Rect& r, float radius, float angle, Color hot,
                                     Color base, float width) {
    if (!dc_ || r.IsEmpty() || hot.a <= 0.0f) return;
    ID2D1LinearGradientBrush* brush = LinearGradientBrush(base, hot, base);
    if (!brush) return;
    // 轴长取半对角线：亮带核心落在描边远端，绕中心旋转即 shimmer。
    const float axis = 0.5f * std::sqrt(r.w * r.w + r.h * r.h) + width;
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    const float dx = std::cos(angle) * axis, dy = std::sin(angle) * axis;
    brush->SetStartPoint(D2D1::Point2F(cx - dx, cy - dy));
    brush->SetEndPoint(D2D1::Point2F(cx + dx, cy + dy));
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    const Rect inset = r.Inset(width * 0.5f, width * 0.5f);
    radius = std::max(0.0f, radius - width * 0.5f);
    dc_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ToD2D(inset), radius, radius}, brush, width,
                              RoundStroke());
}

void Painter::DrawTextGlow(std::wstring_view text, const Rect& r, TextRole role, Color color,
                           Align align, float max_width) {
    if (!dc_ || text.empty() || color.a <= 0.0f) return;
    const float px = 1.0f / scale_;
    const Color halo{color.r, color.g, color.b, color.a * 0.16f};
    static constexpr Point kOffsets[8] = {{-1, 0}, {1, 0},  {0, -1}, {0, 1},
                                          {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (const Point& offset : kOffsets) {
        DrawText(text, {r.x + offset.x * px, r.y + offset.y * px, r.w, r.h}, role, halo, align,
                 max_width);
    }
    DrawText(text, r, role, color, align, max_width);
}

void DrawSpotlight(Painter& painter, const Theme& theme, const Rect& r, float radius,
                   Point center, float intensity) {
    // 渐变外停靠点用同 RGB 零透明：premultiplied 下淡出到纯黑会在光斑边缘挂黑边。
    const Color fill = theme.spotlight_fill;
    const Color border = theme.spotlight_border;
    const float cx = r.x + r.w * 0.5f;
    const float cy = r.y + r.h * 0.5f;
    const Point rim{center.x + (center.x - cx) * 0.06f, center.y + (center.y - cy) * 0.06f};
    painter.FillRoundedRectRadial(r, radius, center, 600.0f,
                                  Color{fill.r, fill.g, fill.b, fill.a * intensity},
                                  Color{fill.r, fill.g, fill.b, 0.0f});
    painter.StrokeRoundedRectRadial(r, radius, rim, 400.0f,
                                    Color{border.r, border.g, border.b, border.a * intensity},
                                    Color{border.r, border.g, border.b, 0.0f});
    painter.StrokeRoundedRect(r, radius, theme.stroke_card);
}

void DrawMnemonicUnderline(Painter& painter, std::wstring_view display, int index, const Rect& r,
                           TextRole role, Color color, Align align, LumaTextBridge* luma) {
    if (index < 0 || index >= static_cast<int>(display.size()) || color.a <= 0.0f) return;
    const float full = AdvanceUiText(display, role, luma);
    const float x_left = AdvanceUiText(display.substr(0, static_cast<size_t>(index)), role, luma);
    const float x_right =
        AdvanceUiText(display.substr(0, static_cast<size_t>(index) + 1), role, luma);
    if (x_right - x_left < 0.5f) return;
    float origin = r.x;
    if (align == Align::Center) origin = r.x + (r.w - full) * 0.5f;
    else if (align == Align::Trailing) origin = r.Right() - full;
    const float text_h = MeasureUiText(display, role, 0.0f, luma).h;
    const float y = Snap(r.y + (r.h + text_h) * 0.5f - 2.0f, painter.Scale());
    // FillRect：RoundStroke 的圆帽会把 1px 线两端各伸出半个笔宽。
    painter.FillRect({origin + x_left, y, x_right - x_left, 1.0f / std::max(painter.Scale(), 0.01f)},
                     color);
}

void DrawElevated(Painter& painter, const Theme& theme, const Rect& r, float radius,
                  Elevation elevation, Color fill, bool wrap_corners) {
    const int i = Clamp(static_cast<int>(elevation), 0, 3);
    const bool custom = fill.a > 0.0f;
    if (!custom) fill = theme.fill_input;
    const float fade = custom ? Clamp(fill.a, 0.0f, 1.0f) : 1.0f;
    const float spread = theme.elevation_spread[i];
    if (spread > 0.01f && fade > 0.01f) {
        Color glow = theme.glow_md;
        glow.a *= theme.elevation_glow[i] * fade;
        painter.DrawGlow(r, radius, glow, spread, wrap_corners);
    }
    painter.FillRoundedRect(r, radius, fill);
    Color spec = theme.specular_line;
    spec.a *= theme.elevation_specular[i] * fade;
    if (spec.a > 0.01f) {
        painter.DrawInnerLight(r, radius, spec, Color{0.0f, 0.0f, 0.0f, 0.35f * spec.a});
    }
    Color stroke = theme.stroke_card;
    stroke.a *= fade;
    painter.StrokeRoundedRect(r, radius, stroke);
}

float MeasureWrappedHeight(std::wstring_view text_view, TextRole role, float width,
                           LumaTextBridge* luma) {
    if (text_view.empty() || !(width > 0.0f)) return 0.0f;
    const float line_h = MeasureUiText(L"m4B", role, 0.0f, luma).h;
    size_t lines = 0;
    EachWrappedLine(text_view, role, width, luma, [&](std::wstring_view) { ++lines; });
    return static_cast<float>(lines) * line_h;
}

} // namespace lumen

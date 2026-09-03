#include "lumen/TitleBar.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/com_ptr.h"
#include "../core/text_service.h"
#include <windows.h>
#include <d2d1_3.h>
#include "lumen/win_undef.h"
#include <wincodec.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace lumen {
namespace {

constexpr float kPadX = 16.0f;
constexpr float kGlyphSlot = 24.0f;
constexpr float kTitleGap = 12.0f;
constexpr float kHudMin = 80.0f;

class HitThroughRow : public Row {
protected:
    bool HitTransparent() const noexcept override { return true; }
};

Color FadeA(Color c, float a) noexcept {
    c.a *= Clamp(a, 0.0f, 1.0f);
    return c;
}

void DrawCaptionSquare(Painter& painter, const Rect& slot, Color color, bool restore,
                       Color punch) {
    const float cx = slot.x + slot.w * 0.5f;
    const float cy = slot.y + slot.h * 0.5f;
    constexpr float kStroke = 1.5f;
    if (!restore) {
        constexpr float s = 10.0f;
        painter.StrokeRoundedRect({cx - s * 0.5f, cy - s * 0.5f, s, s}, 0.0f, color, kStroke);
        return;
    }
    constexpr float s = 8.0f;
    constexpr float o = 3.0f;
    const Rect back{cx - s * 0.5f + o * 0.5f, cy - s * 0.5f - o * 0.5f, s, s};
    const Rect front{cx - s * 0.5f - o * 0.5f, cy - s * 0.5f + o * 0.5f, s, s};
    painter.StrokeRoundedRect(back, 0.0f, color, kStroke);
    painter.FillRect(front, punch);
    painter.StrokeRoundedRect(front, 0.0f, color, kStroke);
}

bool DecodeFrame(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame,
                 std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height) {
    if (!factory || !frame) return false;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }
    UINT next_width = 0, next_height = 0;
    if (FAILED(converter->GetSize(&next_width, &next_height)) || next_width == 0 ||
        next_height == 0) {
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(next_width) * next_height * 4ull;
    if (bytes > static_cast<uint64_t>(UINT_MAX)) return false;
    std::vector<uint8_t> next(static_cast<size_t>(bytes));
    if (FAILED(converter->CopyPixels(nullptr, next_width * 4u, static_cast<UINT>(bytes),
                                     next.data()))) {
        return false;
    }
    pixels = std::move(next);
    width = next_width;
    height = next_height;
    return true;
}

bool DecodeBestFrame(std::span<const std::byte> encoded, std::vector<uint8_t>& pixels,
                     uint32_t& width, uint32_t& height) {
    if (encoded.empty() || encoded.size() > static_cast<size_t>(UINT_MAX)) return false;
    std::vector<uint8_t> copy(encoded.size());
    std::transform(encoded.begin(), encoded.end(), copy.begin(),
                   [](std::byte value) { return std::to_integer<uint8_t>(value); });
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(copy.data(), static_cast<DWORD>(copy.size())))) {
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr,
                                                WICDecodeMetadataCacheOnLoad, &decoder))) {
        return false;
    }
    UINT count = 0;
    if (FAILED(decoder->GetFrameCount(&count)) || count == 0) return false;
    UINT best_index = 0;
    UINT best_area = 0;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(i, &frame))) continue;
        UINT fw = 0, fh = 0;
        if (FAILED(frame->GetSize(&fw, &fh))) continue;
        const UINT area = fw * fh;
        if (area > best_area) {
            best_area = area;
            best_index = i;
        }
    }
    ComPtr<IWICBitmapFrameDecode> best;
    if (FAILED(decoder->GetFrame(best_index, &best))) return false;
    return DecodeFrame(factory.get(), best.get(), pixels, width, height);
}

}  // namespace

struct TitleBar::IconImage {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    ComPtr<ID2D1Bitmap1> bitmap;
    void* device = nullptr;
};

TitleBar::~TitleBar() = default;

bool TitleBar::HasIcon() const noexcept {
    return icon_image_ && icon_image_->width > 0 && icon_image_->height > 0 &&
           !icon_image_->pixels.empty();
}

bool TitleBar::LoadIconMemory(std::span<const std::byte> encoded) {
    auto next = std::make_unique<IconImage>();
    if (!DecodeBestFrame(encoded, next->pixels, next->width, next->height)) {
        return false;
    }
    icon_image_ = std::move(next);
    RelayoutParent();
    Invalidate();
    return true;
}

TitleBar& TitleBar::ClearIcon() {
    if (!icon_image_) return *this;
    icon_image_.reset();
    RelayoutParent();
    Invalidate();
    return *this;
}

float TitleBar::CaptionStart() const noexcept {
    return kPadX + ((HasIcon() || !glyph_.empty()) ? kGlyphSlot : 0.0f);
}

float TitleBar::TitleWidth(float bar_w) const {
    const std::wstring_view text = title_.empty() ? std::wstring_view(L"LUMEN") : title_;
    const float natural = std::max(1.0f, MeasureText(text, TextRole::CaptionStrong).w);
    const float buttons = kButtonWidth * 3.0f;
    const float hud = status_.empty() ? 0.0f : kHudMin;
    const float leftover = bar_w - CaptionStart() - kTitleGap - hud - buttons - 8.0f;
    if (leftover < 1.0f) return natural;
    return std::min(natural, leftover);
}

TitleBar::TitleBar() {
    content_ = &Add<HitThroughRow>();
    content_->AlignCross(StackPanel::CrossAlign::Center);
    content_->Spacing(8.0f);
}

Size TitleBar::Measure(Size available, const Theme& theme) {
    const float h = kHeight;
    const float buttons = kButtonWidth * 3.0f;
    const float x = CaptionStart() + TitleWidth(available.w) + kTitleGap;
    const float content_w = std::max(0.0f, available.w - x - buttons - 8.0f);
    if (content_) MeasureChildAt(0, {content_w, h}, theme);
    return {available.w > 0.0f ? available.w : 320.0f, h};
}

void TitleBar::Arrange(const Rect& absolute) {
    absolute_ = {absolute.x, absolute.y, absolute.w, kHeight};
    const float h = kHeight;
    const float buttons = kButtonWidth * 3.0f;
    const float x = CaptionStart() + TitleWidth(absolute.w) + kTitleGap;
    const float content_w = std::max(0.0f, absolute.w - x - buttons - 8.0f);
    if (content_) {
        SetChildBounds(*content_, {x, 0.0f, content_w, h});
        ArrangeChildAt(0);
    }
}

Rect TitleBar::ButtonSlot(int index) const noexcept {
    const float buttons = kButtonWidth * 3.0f;
    return {absolute_.w - buttons + static_cast<float>(index) * kButtonWidth, 0.0f, kButtonWidth,
            kHeight};
}

TitleBar::Region TitleBar::Hit(Point window_dip) const noexcept {
    const Rect bar{absolute_.x, absolute_.y, absolute_.w, kHeight};
    if (!bar.Contains(window_dip)) return Region::Client;
    const float x = window_dip.x - absolute_.x;
    const float w = absolute_.w;
    if (x >= w - kButtonWidth) return Region::Close;
    if (x >= w - 2.0f * kButtonWidth) return Region::Max;
    if (x >= w - 3.0f * kButtonWidth) return Region::Min;
    return Region::Caption;
}

void TitleBar::SetButtonHover(Region region) {
    if (hover_ == region) return;
    hover_ = region;
    Animate();
    Invalidate();
}

void TitleBar::Maximized(bool value) {
    if (maximized_ == value) return;
    maximized_ = value;
    Invalidate();
}

int TitleBar::HoverIndex() const noexcept {
    if (hover_ == Region::Min) return 0;
    if (hover_ == Region::Max) return 1;
    if (hover_ == Region::Close) return 2;
    return -1;
}

bool TitleBar::OnAnimate(float dt_seconds) {
    bool more = EaseTo(min_glow_, hover_ == Region::Min ? 1.0f : 0.0f, dt_seconds, 16.0f, 0.01f);
    more = EaseTo(max_glow_, hover_ == Region::Max ? 1.0f : 0.0f, dt_seconds, 16.0f, 0.01f) || more;
    more = EaseTo(close_glow_, hover_ == Region::Close ? 1.0f : 0.0f, dt_seconds, 16.0f, 0.01f) ||
           more;
    if (more) Invalidate();
    return more || Panel::OnAnimate(dt_seconds);
}

void TitleBar::Prepare(Painter& painter) {
    if (!HasIcon()) return;
    const void* identity = painter.DeviceIdentity();
    if (!icon_image_->bitmap || icon_image_->device != identity) {
        icon_image_->bitmap.reset();
        icon_image_->bitmap.p = painter.CreateBitmapBgra(
            icon_image_->width, icon_image_->height, icon_image_->pixels.data(),
            icon_image_->width * 4u);
        icon_image_->device = icon_image_->bitmap ? const_cast<void*>(identity) : nullptr;
    }
}

void TitleBar::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    const Rect bar{absolute_.x, absolute_.y, absolute_.w, kHeight};
    painter.FillRect(bar, theme.bg);

    float x = bar.x + CaptionStart();
    if (HasIcon() && icon_image_->bitmap) {
        constexpr float kIconPx = 20.0f;
        const Rect icon_slot{bar.x + kPadX, bar.y + (bar.h - kIconPx) * 0.5f, kIconPx, kIconPx};
        painter.DrawBitmap(icon_image_->bitmap.get(), icon_slot, true);
    } else if (!glyph_.empty()) {
        const Rect icon_slot{bar.x + kPadX, bar.y, 16.0f, bar.h};
        painter.DrawIcon(glyph_, icon_slot, 16.0f, theme.text);
    }
    const float title_w = TitleWidth(bar.w);
    const std::wstring_view caption = title_.empty() ? std::wstring_view(L"LUMEN") : title_;
    painter.DrawText(caption, {x, bar.y, title_w, bar.h}, TextRole::CaptionStrong, theme.text,
                     Align::Leading, title_w);
    x += title_w + kTitleGap;

    const float buttons_w = kButtonWidth * 3.0f;
    const float hud_w = std::max(0.0f, bar.Right() - buttons_w - 8.0f - x);
    if (!status_.empty() && hud_w > 24.0f) {
        painter.DrawText(status_, {x, bar.y, hud_w, bar.h}, TextRole::Mono, theme.text_secondary,
                         Align::Leading, hud_w);
    }

    const float hovers[3] = {min_glow_, max_glow_, close_glow_};
    for (int i = 0; i < 3; ++i) {
        const Rect slot{bar.x + ButtonSlot(i).x, bar.y, kButtonWidth, kHeight};
        if (hovers[i] > 0.01f) {
            painter.FillRect(slot, FadeA(theme.fill_hover, hovers[i]));
        }
        if (i == 1) {
            const Color punch = hovers[i] > 0.01f ? FadeA(theme.fill_hover, hovers[i]) : theme.bg;
            DrawCaptionSquare(painter, slot, theme.text, maximized_, punch);
        } else if (i == 0) {
            painter.DrawIcon(icon::kMinimize, slot, 16.0f, theme.text);
        } else {
            painter.DrawIcon(icon::kClose, slot, 16.0f, theme.text);
        }
    }
}

} // namespace lumen

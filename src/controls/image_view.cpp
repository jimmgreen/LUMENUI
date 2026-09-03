#include "lumen/ImageView.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/com_ptr.h"
#include <windows.h>
#include <d2d1_3.h>
#include "lumen/win_undef.h"
#include <wincodec.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace lumen {

struct ImageView::Impl {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    ComPtr<ID2D1Bitmap1> bitmap;
    void* device = nullptr;

    bool Decode(IWICBitmapDecoder* decoder) {
        if (!decoder) return false;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) return false;
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) {
            return false;
        }
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter))) return false;
        if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA,
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
        bitmap.reset();
        device = nullptr;
        return true;
    }
};

ImageView::ImageView() : impl_(std::make_unique<Impl>()) {}
ImageView::~ImageView() = default;

void ImageView::MarkFailed() {
    if (HasImage()) return;
    failed_ = true;
    Invalidate();
}

bool ImageView::LoadFile(std::wstring_view path) {
    if (path.empty()) {
        MarkFailed();
        return false;
    }
    std::wstring terminated(path);
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        MarkFailed();
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(terminated.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, &decoder))) {
        MarkFailed();
        return false;
    }
    if (!impl_->Decode(decoder.get())) {
        MarkFailed();
        return false;
    }
    failed_ = false;
    RelayoutParent();
    Invalidate();
    return true;
}

bool ImageView::LoadMemory(std::span<const std::byte> encoded) {
    if (encoded.empty() || encoded.size() > static_cast<size_t>(UINT_MAX)) {
        MarkFailed();
        return false;
    }
    std::vector<uint8_t> copy(encoded.size());
    std::transform(encoded.begin(), encoded.end(), copy.begin(),
                   [](std::byte value) { return std::to_integer<uint8_t>(value); });
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        MarkFailed();
        return false;
    }
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(copy.data(), static_cast<DWORD>(copy.size())))) {
        MarkFailed();
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr,
                                                WICDecodeMetadataCacheOnLoad, &decoder))) {
        MarkFailed();
        return false;
    }
    if (!impl_->Decode(decoder.get())) {
        MarkFailed();
        return false;
    }
    failed_ = false;
    RelayoutParent();
    Invalidate();
    return true;
}

ImageView& ImageView::ClearSource() {
    impl_->pixels.clear();
    impl_->width = impl_->height = 0;
    impl_->bitmap.reset();
    impl_->device = nullptr;
    failed_ = false;
    RelayoutParent();
    Invalidate();
    return *this;
}

bool ImageView::HasImage() const noexcept { return impl_->width != 0 && impl_->height != 0; }

ImageStatus ImageView::Status() const noexcept {
    if (HasImage()) return ImageStatus::Ready;
    return failed_ ? ImageStatus::Failed : ImageStatus::Empty;
}

Size ImageView::NaturalPixelSize() const noexcept {
    return {static_cast<float>(impl_->width), static_cast<float>(impl_->height)};
}

ImageView& ImageView::Stretch(ImageStretch value) {
    stretch_ = value;
    Invalidate();
    return *this;
}

ImageView& ImageView::HorizontalAlignment(ImageAlign value) {
    horizontal_ = value;
    Invalidate();
    return *this;
}

ImageView& ImageView::VerticalAlignment(ImageAlign value) {
    vertical_ = value;
    Invalidate();
    return *this;
}

ImageView& ImageView::CornerRadius(float value) {
    corner_radius_ = std::max(0.0f, value);
    Invalidate();
    return *this;
}

ImageView& ImageView::Placeholder(std::wstring_view title, std::wstring_view hint) {
    empty_title_ = title;
    empty_hint_ = hint;
    Invalidate();
    return *this;
}

ImageView& ImageView::ErrorPlaceholder(std::wstring_view title, std::wstring_view hint) {
    failed_title_ = title;
    failed_hint_ = hint;
    Invalidate();
    return *this;
}

Size ImageView::Measure(Size available, const Theme&) {
    if (preferred_.w > 0.0f && preferred_.h > 0.0f) {
        return {std::min(preferred_.w, available.w > 0.0f ? available.w : preferred_.w),
                std::min(preferred_.h, available.h > 0.0f ? available.h : preferred_.h)};
    }
    if (!HasImage()) {
        const float w = bounds_.w > 0.0f ? bounds_.w : 160.0f;
        const float h = bounds_.h > 0.0f ? bounds_.h : 120.0f;
        return {std::min(w, available.w), std::min(h, available.h)};
    }
    const float w = bounds_.w > 0.0f ? bounds_.w : static_cast<float>(impl_->width);
    const float h = bounds_.h > 0.0f ? bounds_.h : static_cast<float>(impl_->height);
    return {std::min(w, available.w), std::min(h, available.h)};
}

void ImageView::Prepare(Painter& painter) {
    if (HasImage()) {
        const void* identity = painter.DeviceIdentity();
        if (!impl_->bitmap || impl_->device != identity) {
            impl_->bitmap.reset();
            impl_->bitmap.p = painter.CreateBitmapBgra(impl_->width, impl_->height,
                                                       impl_->pixels.data(), impl_->width * 4u);
            impl_->device = impl_->bitmap ? const_cast<void*>(identity) : nullptr;
        }
    }
    if (corner_radius_ > 0.0f && !absolute_.IsEmpty()) {
        painter.PrepareRoundedClip(absolute_, corner_radius_);
    }
}

void ImageView::DrawPlaceholder(Painter& painter, const Theme& theme, bool failed) const {
    const float radius = corner_radius_ > 0.0f ? corner_radius_ : theme.radius_control;
    painter.FillRoundedRect(absolute_, radius, theme.fill_input);
    painter.StrokeRoundedRect(absolute_, radius, theme.stroke_card);
    const std::wstring& title = failed ? failed_title_ : empty_title_;
    const std::wstring& hint = failed ? failed_hint_ : empty_hint_;
    const std::wstring_view glyph = failed ? icon::kWarning : icon::kView;
    const float icon_box = std::min(44.0f, std::min(absolute_.w, absolute_.h) * 0.28f);
    const bool show_text = absolute_.h >= 72.0f && absolute_.w >= 80.0f;
    const float stack = icon_box + (show_text ? 8.0f + 18.0f + (!hint.empty() ? 16.0f : 0.0f) : 0.0f);
    float y = absolute_.y + (absolute_.h - stack) * 0.5f;
    const Rect icon_r{absolute_.x + (absolute_.w - icon_box) * 0.5f, y, icon_box, icon_box};
    painter.FillRoundedRect(icon_r, icon_box * 0.5f, theme.fill_hover);
    painter.DrawIcon(glyph, icon_r, std::max(14.0f, icon_box * 0.42f),
                     failed ? theme.text : theme.text_secondary);
    if (!show_text) return;
    y += icon_box + 8.0f;
    painter.DrawText(title, {absolute_.x + 12.0f, y, absolute_.w - 24.0f, 18.0f},
                     TextRole::CaptionStrong, theme.text, Align::Center);
    if (!hint.empty()) {
        painter.DrawText(hint, {absolute_.x + 12.0f, y + 18.0f, absolute_.w - 24.0f, 16.0f},
                         TextRole::Caption, theme.text_secondary, Align::Center);
    }
}

void ImageView::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    if (!HasImage() || !impl_->bitmap) {
        DrawPlaceholder(painter, theme, failed_);
        return;
    }
    const float natural_w = static_cast<float>(impl_->width);
    const float natural_h = static_cast<float>(impl_->height);
    float width = absolute_.w;
    float height = absolute_.h;
    if (stretch_ == ImageStretch::None) {
        width = natural_w;
        height = natural_h;
    } else if (stretch_ != ImageStretch::Fill) {
        const float sx = absolute_.w / natural_w;
        const float sy = absolute_.h / natural_h;
        const float scale = stretch_ == ImageStretch::Uniform ? std::min(sx, sy) : std::max(sx, sy);
        width = natural_w * scale;
        height = natural_h * scale;
    }
    const auto offset = [](float outer, float inner, ImageAlign align) {
        if (align == ImageAlign::End) return outer - inner;
        if (align == ImageAlign::Center) return (outer - inner) * 0.5f;
        return 0.0f;
    };
    const Rect dest{absolute_.x + offset(absolute_.w, width, horizontal_),
                    absolute_.y + offset(absolute_.h, height, vertical_), width, height};
    if (corner_radius_ > 0.0f) painter.PushRoundedClip(absolute_, corner_radius_);
    else if (stretch_ == ImageStretch::UniformToFill || stretch_ == ImageStretch::None) {
        painter.PushClip(absolute_);
    }
    painter.DrawBitmap(impl_->bitmap.get(), dest, true);
    if (corner_radius_ > 0.0f) painter.PopRoundedClip();
    else if (stretch_ == ImageStretch::UniformToFill || stretch_ == ImageStretch::None) {
        painter.PopClip();
    }
}

} // namespace lumen

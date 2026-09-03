// lumen/ImageView.h — WIC 静态图片：同步解码、设备恢复重建与四种缩放模式。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace lumen {

enum class ImageStretch { Uniform, UniformToFill, Fill, None };
enum class ImageAlign { Start, Center, End };
enum class ImageStatus { Empty, Ready, Failed };

class ImageView : public ControlOf<ImageView> {
public:
    ImageView();
    ~ImageView() override;

    bool LoadFile(std::wstring_view path);
    bool LoadMemory(std::span<const std::byte> encoded);
    ImageView& ClearSource();
    bool HasImage() const noexcept;
    ImageStatus Status() const noexcept;
    Size NaturalPixelSize() const noexcept;

    ImageView& Stretch(ImageStretch value);
    ImageView& HorizontalAlignment(ImageAlign value);
    ImageView& VerticalAlignment(ImageAlign value);
    ImageView& CornerRadius(float value);
    ImageView& Placeholder(std::wstring_view title, std::wstring_view hint = {});
    ImageView& ErrorPlaceholder(std::wstring_view title, std::wstring_view hint = {});

    ImageView& PreferredSize(Size size) {
        preferred_ = size;
        RelayoutParent();
        return *this;
    }
    Size PreferredSize() const noexcept { return preferred_; }

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Prepare(Painter& painter) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }

private:
    void MarkFailed();
    void DrawPlaceholder(Painter& painter, const Theme& theme, bool failed) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    ImageStretch stretch_ = ImageStretch::Uniform;
    ImageAlign horizontal_ = ImageAlign::Center;
    ImageAlign vertical_ = ImageAlign::Center;
    float corner_radius_ = 0.0f;
    Size preferred_{};
    bool failed_ = false;
    std::wstring empty_title_{L"No image"};
    std::wstring empty_hint_{L"Load a file or bitmap"};
    std::wstring failed_title_{L"Couldn't load"};
    std::wstring failed_hint_{L"Source missing or invalid"};
};

} // namespace lumen

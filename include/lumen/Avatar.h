// lumen/Avatar.h — 头像：单色圆 + 首字/图标 + 可选在线状态点。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include <algorithm>

namespace lumen {

class Avatar : public ControlOf<Avatar> {
public:
    enum class Presence { None, Online, Away, Busy };

    Avatar() = default;
    explicit Avatar(std::wstring_view name) { Name(name); }

    // 展示名：圆内取 CJK 首字 / 拉丁首字母（大写）。
    Avatar& Name(std::wstring_view value);
    const std::wstring& Name() const noexcept { return name_; }
    // 图标覆盖首字（kContact 等）。
    Avatar& Glyph(std::wstring_view value) {
        glyph_ = value;
        Invalidate();
        return *this;
    }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    // 直径（DIP）。状态点直径随直径缩放。
    Avatar& Diameter(float value) {
        diameter_ = std::max(20.0f, value);
        RelayoutParent();
        return *this;
    }
    float Diameter() const noexcept { return diameter_; }
    Avatar& PresenceState(Presence value) {
        presence_ = value;
        Invalidate();
        return *this;
    }
    Presence PresenceState() const noexcept { return presence_; }

protected:
    friend class WindowImpl;
    Size Measure(Size, const Theme&) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }

private:
    std::wstring name_;
    std::wstring initials_;   // Name() 赋值时算好，绘制路径零分配
    std::wstring glyph_;
    float diameter_ = 32.0f;
    Presence presence_ = Presence::None;
};

} // namespace lumen

// lumen/InfoBadge.h — 叠在导航项 / 标签 / 图标上的圆点或数字角标（不是流式 Badge 胶囊）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include <string>
#include <string_view>

namespace lumen {

struct InfoBadgeData {
    enum class Kind { None, Dot, Count, Icon };
    Kind kind = Kind::None;
    int count = 0;
    std::wstring glyph;
    int overflow = 99;

    static InfoBadgeData Dot() { return {.kind = Kind::Dot}; }
    static InfoBadgeData Count(int n) { return {.kind = Kind::Count, .count = n}; }
    static InfoBadgeData Icon(std::wstring_view value) {
        InfoBadgeData data;
        data.kind = Kind::Icon;
        data.glyph.assign(value.data(), value.size());
        return data;
    }

    bool Empty() const noexcept {
        if (kind == Kind::None) return true;
        if (kind == Kind::Count && count < 0) return true;
        if (kind == Kind::Icon && glyph.empty()) return true;
        return false;
    }
};

Size MeasureInfoBadge(const InfoBadgeData& data);
// center 为窗口 DIP，角标以其几何中心落点。
void PaintInfoBadge(Painter& painter, const Theme& theme, Point center, const InfoBadgeData& data);

inline Point InfoBadgeCorner(const Rect& host) noexcept {
    return {host.Right() - 1.0f, host.y + 1.0f};
}

class InfoBadge : public ControlOf<InfoBadge> {
public:
    InfoBadge() = default;
    explicit InfoBadge(int count) : data_(InfoBadgeData::Count(count)) {}

    InfoBadge& Data(InfoBadgeData value) {
        data_ = std::move(value);
        RelayoutParent();
        return *this;
    }
    const InfoBadgeData& Data() const noexcept { return data_; }
    InfoBadge& Count(int n) { return Data(InfoBadgeData::Count(n)); }
    InfoBadge& Dot() { return Data(InfoBadgeData::Dot()); }
    InfoBadge& Glyph(std::wstring_view value) { return Data(InfoBadgeData::Icon(value)); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }
    float ChromeRadius(const Theme& theme) const noexcept override {
        (void)theme;
        return absolute_.h * 0.5f;
    }

    void RelayoutParent();

    InfoBadgeData data_;
};

} // namespace lumen

#include "lumen/InfoBadge.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include <algorithm>

namespace lumen {
namespace {

constexpr float kDot = 8.0f;
constexpr float kIcon = 16.0f;
constexpr float kCountH = 16.0f;
constexpr float kCountPadX = 5.0f;

std::wstring_view CountText(const InfoBadgeData& data, wchar_t (&buf)[16]) noexcept {
    buf[0] = L'\0';
    if (data.kind != InfoBadgeData::Kind::Count) return {};
    int n = data.count;
    bool plus = false;
    const int cap = data.overflow < 0 ? 0 : data.overflow;
    if (n > cap) {
        n = cap;
        plus = true;
    }
    if (n < 0) n = 0;
    wchar_t digits[12];
    int nd = 0;
    int v = n;
    do {
        digits[nd++] = static_cast<wchar_t>(L'0' + (v % 10));
        v /= 10;
    } while (v != 0 && nd < 11);
    int o = 0;
    while (nd) buf[o++] = digits[--nd];
    if (plus) buf[o++] = L'+';
    buf[o] = L'\0';
    return {buf, static_cast<size_t>(o)};
}

} // namespace

Size MeasureInfoBadge(const InfoBadgeData& data) {
    if (data.Empty()) return {};
    switch (data.kind) {
    case InfoBadgeData::Kind::Dot:
        return {kDot, kDot};
    case InfoBadgeData::Kind::Icon:
        return {kIcon, kIcon};
    case InfoBadgeData::Kind::Count: {
        wchar_t buf[16];
        const float tw = MeasureUiText(CountText(data, buf), TextRole::Caption).w;
        return {std::max(kCountH, tw + kCountPadX * 2.0f), kCountH};
    }
    default:
        return {};
    }
}

void PaintInfoBadge(Painter& painter, const Theme& theme, Point center, const InfoBadgeData& data) {
    if (data.Empty()) return;
    const Size sz = MeasureInfoBadge(data);
    const Rect r{center.x - sz.w * 0.5f, center.y - sz.h * 0.5f, sz.w, sz.h};
    const float radius = sz.h * 0.5f;
    painter.DrawGlow(r, radius, theme.glow_sm);
    painter.FillRoundedRect(r, radius, theme.accent);
    if (data.kind == InfoBadgeData::Kind::Count) {
        wchar_t buf[16];
        painter.DrawText(CountText(data, buf), r, TextRole::Caption, theme.primary_text, Align::Center);
    } else if (data.kind == InfoBadgeData::Kind::Icon) {
        painter.DrawIcon(data.glyph, r, 10.0f, theme.primary_text);
    }
}

void InfoBadge::RelayoutParent() { Control::RelayoutParent(); }

Size InfoBadge::Measure(Size, const Theme&) { return MeasureInfoBadge(data_); }

void InfoBadge::Draw(Painter& painter, const Theme& theme) {
    PaintInfoBadge(painter, theme, {absolute_.x + absolute_.w * 0.5f,
                                    absolute_.y + absolute_.h * 0.5f},
                   data_);
}

} // namespace lumen

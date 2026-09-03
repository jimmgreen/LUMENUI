#include "lumen/StatusBar.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include <algorithm>

namespace lumen {
namespace {
constexpr float kPad = 10.0f;
constexpr float kGlyph = 14.0f;
constexpr float kGlyphGap = 6.0f;
constexpr float kSep = 9.0f;
constexpr float kMinPath = 48.0f;
const std::wstring kEmpty{};
} // namespace

void StatusBar::RelayoutParent() { Control::RelayoutParent(); }

StatusBar::Item* StatusBar::Find(std::wstring_view id) noexcept {
    for (Item& item : items_) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

const StatusBar::Item* StatusBar::Find(std::wstring_view id) const noexcept {
    for (const Item& item : items_) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

StatusBar& StatusBar::Upsert(std::wstring_view id, std::wstring_view text, StatusBarAlign align,
                             std::wstring_view glyph) {
    if (Item* item = Find(id)) {
        const bool same = item->text == text && item->align == align &&
                          (glyph.empty() || item->glyph == glyph);
        if (same) return *this;
        item->text = text;
        item->align = align;
        if (!glyph.empty()) item->glyph = glyph;
        RelayoutParent();
        Invalidate();
        return *this;
    }
    Item row;
    row.id = id;
    row.text = text;
    row.glyph = glyph;
    row.align = align;
    if (align == StatusBarAlign::Leading && id == L"path") {
        items_.insert(items_.begin(), std::move(row));
    } else {
        items_.push_back(std::move(row));
    }
    RelayoutParent();
    Invalidate();
    return *this;
}

StatusBar& StatusBar::AddItem(std::wstring_view id, std::wstring_view text, StatusBarAlign align) {
    return Upsert(id, text, align);
}

StatusBar& StatusBar::ItemText(std::wstring_view id, std::wstring_view text) {
    if (Item* item = Find(id)) {
        if (item->text == text) return *this;
        item->text = text;
        RelayoutParent();
        Invalidate();
    }
    return *this;
}

StatusBar& StatusBar::ItemGlyph(std::wstring_view id, std::wstring_view glyph) {
    if (Item* item = Find(id)) {
        if (item->glyph == glyph) return *this;
        item->glyph = glyph;
        RelayoutParent();
        Invalidate();
    }
    return *this;
}

bool StatusBar::RemoveItem(std::wstring_view id) {
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id != id) continue;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
        if (hover_ == static_cast<int>(i)) hover_ = -1;
        else if (hover_ > static_cast<int>(i)) --hover_;
        RelayoutParent();
        Invalidate();
        return true;
    }
    return false;
}

const std::wstring& StatusBar::ItemText(std::wstring_view id) const {
    if (const Item* item = Find(id)) return item->text;
    return kEmpty;
}

StatusBar& StatusBar::Path(std::wstring_view text) {
    return Upsert(L"path", text, StatusBarAlign::Leading, icon::kFolder);
}

const std::wstring& StatusBar::Path() const { return ItemText(L"path"); }

StatusBar& StatusBar::Zoom(std::wstring_view text) {
    return Upsert(L"zoom", text, StatusBarAlign::Trailing);
}

const std::wstring& StatusBar::Zoom() const { return ItemText(L"zoom"); }

StatusBar& StatusBar::CountText(std::wstring_view text) {
    return Upsert(L"count", text, StatusBarAlign::Trailing);
}

const std::wstring& StatusBar::CountText() const { return ItemText(L"count"); }

float StatusBar::PreferredWidth(const Item& item) const {
    float w = kPad * 2.0f;
    if (!item.glyph.empty()) w += kGlyph + kGlyphGap;
    if (!item.text.empty()) w += MeasureText(item.text, TextRole::Caption).w;
    return std::max(w, 28.0f);
}

void StatusBar::Rebuild(float width) {
    slots_.assign(items_.size(), {});
    seps_.clear();
    if (items_.empty() || width < 1.0f) return;

    std::vector<float> pref(items_.size(), 0.0f);
    float lead = 0.0f;
    int leading_n = 0;
    int trailing_n = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        pref[i] = PreferredWidth(items_[i]);
        if (items_[i].align == StatusBarAlign::Trailing) {
            ++trailing_n;
        } else {
            lead += pref[i];
            ++leading_n;
        }
    }
    if (leading_n > 1) lead += kSep * static_cast<float>(leading_n - 1);

    float x = width - kPad;
    bool first_trail = true;
    for (size_t i = items_.size(); i-- > 0;) {
        if (items_[i].align != StatusBarAlign::Trailing) continue;
        if (!first_trail) {
            x -= kSep;
            seps_.push_back(x + (kSep - 1.0f) * 0.5f);
        }
        first_trail = false;
        x -= pref[i];
        slots_[i] = {x, 0.0f, pref[i], kHeight};
    }

    const float trail_left = trailing_n > 0 ? x - kPad : width - kPad;
    float grow = 0.0f;
    if (leading_n > 0) grow = std::max(0.0f, trail_left - kPad - lead);

    x = kPad;
    bool first_lead = true;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].align != StatusBarAlign::Leading) continue;
        if (!first_lead) {
            seps_.push_back(x + (kSep - 1.0f) * 0.5f);
            x += kSep;
        }
        float w = pref[i];
        if (first_lead) {
            w = std::max(kMinPath, w + grow);
            first_lead = false;
        }
        slots_[i] = {x, 0.0f, w, kHeight};
        x += w;
    }
}

Size StatusBar::Measure(Size available, const Theme&) {
    const float width = (available.w >= 0.0f && available.w < 1.0e4f) ? available.w : 280.0f;
    Rebuild(width);
    return {width, kHeight};
}

void StatusBar::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    Rebuild(absolute.w);
}

int StatusBar::HitIndex(Point local) const noexcept {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].Contains(local)) return static_cast<int>(i);
    }
    return -1;
}

void StatusBar::Draw(Painter& painter, const Theme& theme) {
    painter.FillRect(absolute_, theme.fill_input);
    painter.FillRect({absolute_.x, absolute_.y, absolute_.w, 1.0f}, theme.stroke_divider);
    const Color fg = enabled_ ? theme.text_secondary : theme.text_disabled;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Rect slot = slots_[i].Offset(absolute_.x, absolute_.y);
        if (slot.IsEmpty()) continue;
        const Item& item = items_[i];
        if (hover_ == static_cast<int>(i) && !invoked_.Empty()) {
            painter.FillRoundedRect(slot.Inset(2.0f, 3.0f), 4.0f, theme.fill_hover);
        }
        float x = slot.x + kPad;
        const float cy = slot.y + (slot.h - kGlyph) * 0.5f;
        if (!item.glyph.empty()) {
            painter.DrawIcon(item.glyph, {x, cy, kGlyph, kGlyph}, kGlyph, fg);
            x += kGlyph + kGlyphGap;
        }
        const float text_w = std::max(0.0f, slot.Right() - kPad - x);
        if (!item.text.empty() && text_w > 4.0f) {
            painter.DrawText(item.text, {x, slot.y, text_w, slot.h}, TextRole::Caption, fg,
                             Align::Leading, text_w);
        }
    }
    for (float sx : seps_) {
        painter.FillRect({absolute_.x + sx, absolute_.y + 8.0f, 1.0f, kHeight - 16.0f},
                         theme.stroke_divider);
    }
}

void StatusBar::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    const int hit = HitIndex(local);
    if (hit == hover_) return;
    hover_ = hit;
    Invalidate();
}

void StatusBar::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_ < 0) return;
    hover_ = -1;
    Invalidate();
}

void StatusBar::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001) || invoked_.Empty()) return;
    const int hit = HitIndex(local);
    if (hit < 0) return;
    invoked_.Emit(items_[static_cast<size_t>(hit)].id);
}

CursorShape StatusBar::CursorAt(Point local) const {
    if (!invoked_.Empty() && HitIndex(local) >= 0) return CursorShape::Hand;
    return CursorShape::Arrow;
}

} // namespace lumen

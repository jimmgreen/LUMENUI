#include "lumen/LogView.h"
#include "lumen/Clipboard.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace lumen {
namespace {
constexpr float kBarHit = 10.0f;
constexpr std::wstring_view kLevels[]{L"DEBUG", L"INFO", L"WARN", L"ERROR"};
size_t LevelIndex(LogLevel level) { return std::min(static_cast<size_t>(level), size_t{3}); }
std::wstring Fold(std::wstring_view text) {
    std::wstring result(text);
    for (auto& ch : result) ch = static_cast<wchar_t>(std::towlower(ch));
    return result;
}
} // namespace

void LogView::RelayoutParent() { Control::RelayoutParent(); }

LogView& LogView::LineText(std::function<void(size_t, std::wstring&)> provider) {
    line_text_ = std::move(provider);
    entry_ = {};
    return Refresh();
}

LogView& LogView::LineLevel(std::function<LogLevel(size_t)> provider) {
    line_level_ = std::move(provider);
    return Refresh();
}

LogView& LogView::Entry(std::function<void(size_t, LogEntry&)> provider) {
    entry_ = std::move(provider);
    return Refresh();
}

void LogView::ReadEntry(size_t source, LogEntry& out) const {
    out.timestamp.clear(); out.source.clear(); out.message.clear(); out.trace.clear();
    out.level = LogLevel::Info;
    if (entry_) entry_(source, out);
    else {
        if (line_text_) line_text_(source, out.message);
        if (line_level_) out.level = line_level_(source);
    }
}

LogView& LogView::Query(std::wstring_view query) {
    if (query_ == query) return *this;
    query_ = query;
    return Refresh();
}

LogView& LogView::LevelEnabled(LogLevel level, bool enabled) {
    const size_t index = static_cast<size_t>(level);
    if (index >= levels_.size() || levels_[index] == enabled) return *this;
    levels_[index] = enabled;
    return Refresh();
}

bool LogView::LevelEnabled(LogLevel level) const noexcept {
    const size_t index = static_cast<size_t>(level);
    return index < levels_.size() && levels_[index];
}

size_t LogView::LevelCount(LogLevel level) const noexcept {
    const size_t index = static_cast<size_t>(level);
    return index < level_counts_.size() ? level_counts_[index] : 0;
}

void LogView::RebuildView(size_t begin) {
    const size_t selected_source = selected_ >= 0 ? DataIndex(static_cast<size_t>(selected_)) : item_count_;
    if (begin == 0) { visible_.clear(); level_counts_.fill(0); }
    const std::wstring query = Fold(query_);
    LogEntry entry;
    for (size_t row = begin; row < item_count_; ++row) {
        if (entry_ || !query.empty()) ReadEntry(row, entry);
        else entry.level = line_level_ ? line_level_(row) : LogLevel::Info;
        const size_t level = LevelIndex(entry.level);
        const auto matches = [&](std::wstring_view text) { return Fold(text).find(query) != std::wstring::npos; };
        if (!query.empty() && !matches(entry.timestamp) && !matches(kLevels[level]) &&
            !matches(entry.source) && !matches(entry.message) && !matches(entry.trace)) continue;
        ++level_counts_[level];
        if (levels_[level]) visible_.push_back(row);
    }
    selected_ = -1;
    if (selected_source < item_count_) {
        const auto it = std::lower_bound(visible_.begin(), visible_.end(), selected_source);
        if (it != visible_.end() && *it == selected_source) selected_ = it - visible_.begin();
    }
    hover_row_ = -1;
    ClampScroll();
    if (Following()) ScrollToEnd();
    RelayoutParent();
    Invalidate();
    view_changed_.Emit();
}

LogView& LogView::Refresh() { RebuildView(); return *this; }

LogView& LogView::ItemCount(size_t count) {
    const size_t old_count = item_count_;
    item_count_ = count;
    RebuildView(count > old_count ? old_count : 0);
    return *this;
}

LogView& LogView::Follow(bool on) {
    follow_ = on;
    following_ = on;
    if (on) ScrollToEnd();
    Invalidate();
    return *this;
}

void LogView::UpdateFollowing(bool following) {
    following = follow_ && following;
    if (following_ == following) return;
    following_ = following;
    following_changed_.Emit(following);
}

void LogView::Arrange(const Rect& absolute) {
    Control::Arrange(absolute);
    ClampScroll();
    if (Following()) ScrollToEnd();
}

std::wstring LogView::FormatEntry(size_t source) const {
    LogEntry entry;
    ReadEntry(source, entry);
    if (!entry_) return entry.message;
    std::wstring text = entry.timestamp + L" " + std::wstring(kLevels[LevelIndex(entry.level)]) +
                        L" " + entry.source + L" " + entry.message;
    if (!entry.trace.empty()) text += L" " + entry.trace;
    return text;
}

std::wstring LogView::AutomationItemName(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= visible_.size()) return {};
    return FormatEntry(DataIndex(static_cast<size_t>(index)));
}

bool LogView::AutomationSelectIndex(int index) {
    if (!enabled_ || index < -1 || (index >= 0 && static_cast<size_t>(index) >= visible_.size())) return false;
    SelectedIndex(index);
    return true;
}

LogView& LogView::SelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(visible_.size())) return *this;
    selected_ = index;
    if (index >= 0) {
        const float row_h = RowHeight();
        const float top = static_cast<float>(index) * row_h;
        const float bottom = top + row_h;
        if (top < scroll_offset_) target_offset_ = top;
        else if (bottom > scroll_offset_ + absolute_.h) {
            target_offset_ = bottom - absolute_.h;
        }
        Animate();
    }
    Invalidate();
    return *this;
}

bool LogView::CopySelection() const {
    if (selected_ < 0 || static_cast<size_t>(selected_) >= visible_.size()) return false;
    return clipboard::Text(FormatEntry(DataIndex(static_cast<size_t>(selected_))));
}

Size LogView::Measure(Size available, const Theme&) {
    const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 320.0f;
    const float h = (available.h > 0.0f && available.h < 1.0e4f) ? available.h : 180.0f;
    return {w, h};
}

float LogView::ContentHeight() const noexcept {
    return static_cast<float>(visible_.size()) * RowHeight();
}

float LogView::MaxScroll() const {
    return std::max(0.0f, ContentHeight() - absolute_.h);
}

void LogView::ClampScroll() {
    const float max_s = MaxScroll();
    target_offset_ = Clamp(target_offset_, 0.0f, max_s);
    scroll_offset_ = Clamp(scroll_offset_, 0.0f, max_s);
}

void LogView::ScrollToEnd() {
    target_offset_ = MaxScroll();
    if (!window_) scroll_offset_ = target_offset_;
    else Animate();
}

void LogView::PauseFollowIfScrolled() {
    if (!follow_) return;
    UpdateFollowing(target_offset_ >= MaxScroll() - 2.0f);
}

ptrdiff_t LogView::RowAt(Point local) const {
    if (local.y < 0.0f || local.y >= absolute_.h) return -1;
    const ptrdiff_t row =
        static_cast<ptrdiff_t>((local.y + scroll_offset_) / std::max(RowHeight(), 1.0f));
    if (row < 0 || row >= static_cast<ptrdiff_t>(visible_.size())) return -1;
    return row;
}

Rect LogView::VerticalTrack() const noexcept {
    return {absolute_.Right() - kBarHit, absolute_.y, kBarHit, absolute_.h};
}

ScrollThumb LogView::Thumb(float expand) const noexcept {
    return MakeScrollThumb(absolute_, ContentHeight(), scroll_offset_, expand, true);
}

bool LogView::BeginScrollDrag(Point local) {
    if (MaxScroll() <= 0.5f) return false;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (!VerticalTrack().Contains(world)) return false;
    const ScrollThumb thumb = Thumb(1.0f);
    dragging_ = true;
    UpdateFollowing(false);
    if (thumb.visible && thumb.rect.Contains(world)) {
        drag_grab_ = world.y - thumb.rect.y;
    } else {
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, absolute_.h - thumb_h);
        const float t = Clamp((local.y - thumb_h * 0.5f) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        drag_grab_ = thumb_h * 0.5f;
        Invalidate();
    }
    Animate();
    return true;
}

bool LogView::CapturesOverlay(Point p) const {
    if (dragging_) return true;
    if (MaxScroll() <= 0.5f) return false;
    return VerticalTrack().Contains(p);
}

void LogView::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

bool LogView::OnAnimate(float dt_seconds) {
    bool moving = EaseTo(scroll_offset_, target_offset_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(expand_progress_, (hovered_ || dragging_) ? 1.0f : 0.0f, dt_seconds, 18.0f);
    if (moving) Invalidate();
    const bool base = Control::OnAnimate(dt_seconds);
    return moving || base;
}

bool LogView::OnKey(uint32_t vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && vk == 'C') {
        CopySelection();
        return true;
    }
    if (visible_.empty()) return false;
    const ptrdiff_t page =
        std::max(ptrdiff_t{1}, static_cast<ptrdiff_t>(absolute_.h / RowHeight()));
    switch (vk) {
    case VK_DOWN:
        SelectedIndex(selected_ < 0 ? 0 : std::min(selected_ + 1,
                                                      static_cast<ptrdiff_t>(visible_.size()) - 1));
        UpdateFollowing(false);
        return true;
    case VK_UP:
        SelectedIndex(selected_ < 0 ? 0 : std::max(selected_ - 1, ptrdiff_t{0}));
        UpdateFollowing(false);
        return true;
    case VK_NEXT:
        SelectedIndex(Clamp(selected_ + page, ptrdiff_t{0},
                               static_cast<ptrdiff_t>(visible_.size()) - 1));
        UpdateFollowing(false);
        return true;
    case VK_PRIOR:
        SelectedIndex(Clamp(selected_ - page, ptrdiff_t{0},
                               static_cast<ptrdiff_t>(visible_.size()) - 1));
        UpdateFollowing(false);
        return true;
    case VK_END:
        if (visible_.size()) {
            SelectedIndex(static_cast<ptrdiff_t>(visible_.size()) - 1);
            ScrollToEnd();
            follow_ = true;
            UpdateFollowing(true);
        }
        return true;
    case VK_HOME:
        if (visible_.size()) {
            SelectedIndex(0);
            target_offset_ = 0.0f;
            UpdateFollowing(false);
            Animate();
        }
        return true;
    default:
        return false;
    }
}

void LogView::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    if (BeginScrollDrag(local)) return;
    const ptrdiff_t row = RowAt(local);
    if (row >= 0) SelectedIndex(row);
}

void LogView::OnMouseMove(Point local, uint32_t) {
    if (dragging_) {
        const ScrollThumb thumb = Thumb(1.0f);
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, absolute_.h - thumb_h);
        const float t = Clamp((local.y - drag_grab_) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        PauseFollowIfScrolled();
        Invalidate();
        return;
    }
    const ptrdiff_t row = RowAt(local);
    if (row != hover_row_) {
        hover_row_ = row;
        Animate();
        Invalidate();
    }
}

void LogView::OnMouseUp(Point, uint32_t) {
    dragging_ = false;
    PauseFollowIfScrolled();
    Animate();
}

void LogView::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_row_ = -1;
    Animate();
    Invalidate();
}

bool LogView::OnWheel(float delta) {
    if (MaxScroll() <= 0.0f) return false;
    target_offset_ = Clamp(target_offset_ - delta * RowHeight() * 3.0f, 0.0f, MaxScroll());
    PauseFollowIfScrolled();
    Animate();
    return true;
}

LogView::Fields LogView::EntryFields(float y, bool has_trace) const noexcept {
    Fields fields{};
    float x = absolute_.x + 10.0f;
    const float right = std::max(x, absolute_.Right() - kBarHit - 8.0f);
    const auto take = [&](float width) {
        const float w = std::max(0.0f, std::min(width, right - x));
        Rect rect{x, y, w, RowHeight()};
        x += w + 8.0f;
        return rect;
    };
    if (absolute_.w >= 420.0f) fields.time = take(106.0f);
    fields.level = take(52.0f);
    if (absolute_.w >= 600.0f) fields.source = take(94.0f);
    const float trace_width = has_trace && absolute_.w >= 780.0f ? 142.0f : 0.0f;
    fields.message = {x, y, std::max(0.0f, right - x - trace_width - (trace_width > 0 ? 12.0f : 0.0f)), RowHeight()};
    if (trace_width > 0) fields.trace = {right - trace_width, y + 3.0f, trace_width, RowHeight() - 6.0f};
    return fields;
}

void LogView::Prepare(Painter& painter, const Theme& theme) {
    for (Color color : {theme.fill_input, theme.fill_input_hover, theme.fill_selected, theme.fill_hover,
                        theme.text, theme.text_secondary, theme.text_disabled, theme.stroke_divider,
                        theme.scrollbar_thumb, theme.scrollbar_thumb_hover, theme.accent}) painter.PrepareColor(color);
    ClampScroll();
    prepared_first_ = static_cast<size_t>(scroll_offset_ / RowHeight());
    const size_t available = visible_.size() - std::min(prepared_first_, visible_.size());
    prepared_.resize(std::min(available, static_cast<size_t>(std::max(0.0f, absolute_.h) / RowHeight()) + 2));
    for (size_t i = 0; i < prepared_.size(); ++i) {
        auto& entry = prepared_[i];
        ReadEntry(DataIndex(prepared_first_ + i), entry);
        const float y = absolute_.y + static_cast<float>(prepared_first_ + i) * RowHeight() - scroll_offset_;
        const Color level_color = entry.level == LogLevel::Error ? theme.text :
                                  entry.level == LogLevel::Debug ? theme.text_disabled : theme.text_secondary;
        if (!entry_) {
            painter.PrepareText(entry.message, {absolute_.x + 10, y, std::max(0.0f, absolute_.w - 28), RowHeight()}, TextRole::Mono, level_color);
            continue;
        }
        const Fields fields = EntryFields(y, !entry.trace.empty());
        if (!fields.time.IsEmpty()) painter.PrepareText(entry.timestamp, fields.time, TextRole::Mono, theme.text_disabled);
        painter.PrepareText(kLevels[LevelIndex(entry.level)], fields.level, TextRole::Mono, level_color);
        if (!fields.source.IsEmpty()) painter.PrepareText(entry.source, fields.source, TextRole::Mono, theme.text_secondary);
        painter.PrepareText(entry.message, fields.message, TextRole::Mono, theme.text);
        if (!fields.trace.IsEmpty()) painter.PrepareText(entry.trace, fields.trace.Inset(8.0f, 0), TextRole::Mono, theme.text_secondary);
    }
    if (visible_.empty()) painter.PrepareText(empty_text_, absolute_.Inset(10.0f, 0), TextRole::Body, theme.text_secondary);
}

void LogView::Draw(Painter& painter, const Theme& theme) {
    painter.PushClip(absolute_);
    painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);
    for (size_t i = 0; i < prepared_.size(); ++i) {
        const ptrdiff_t row = static_cast<ptrdiff_t>(prepared_first_ + i);
        const auto& entry = prepared_[i];
        const float y = absolute_.y + static_cast<float>(row) * RowHeight() - scroll_offset_;
        const Rect slot{absolute_.x + 4.0f, y, std::max(0.0f, absolute_.w - 8.0f), RowHeight()};
        if (row == selected_) painter.FillRoundedRect(slot, 4.0f, theme.fill_selected);
        else if (row == hover_row_ && enabled_) painter.FillRoundedRect(slot, 4.0f, theme.fill_hover);
        else if (entry_ && entry.level == LogLevel::Error) painter.FillRect(slot, theme.fill_input_hover);
        if (entry_ && (entry.level == LogLevel::Error || entry.level == LogLevel::Warn))
            painter.FillRect({slot.x, slot.y + 5.0f, 2.0f, slot.h - 10.0f},
                             entry.level == LogLevel::Error ? theme.text : theme.text_secondary);
        const Color level_color = entry.level == LogLevel::Error ? theme.text :
                                  entry.level == LogLevel::Debug ? theme.text_disabled : theme.text_secondary;
        if (!entry_) {
            painter.DrawText(entry.message, {slot.x + 6.0f, slot.y, std::max(0.0f, slot.w - 20.0f), slot.h}, TextRole::Mono, level_color);
        } else {
            const Fields fields = EntryFields(y, !entry.trace.empty());
            if (!fields.time.IsEmpty()) painter.DrawText(entry.timestamp, fields.time, TextRole::Mono, theme.text_disabled);
            painter.DrawText(kLevels[LevelIndex(entry.level)], fields.level, TextRole::Mono, level_color);
            if (!fields.source.IsEmpty()) painter.DrawText(entry.source, fields.source, TextRole::Mono, theme.text_secondary);
            painter.DrawText(entry.message, fields.message, TextRole::Mono, theme.text);
            if (!fields.trace.IsEmpty()) {
                painter.StrokeRoundedRect(fields.trace, fields.trace.h * 0.5f, theme.stroke_divider);
                painter.DrawText(entry.trace, fields.trace.Inset(8.0f, 0), TextRole::Mono, theme.text_secondary);
            }
        }
        if (row == selected_ && FocusVisible()) PaintFocusRing(painter, theme, slot.Inset(1.0f, 1.0f), 4.0f);
    }
    if (visible_.empty()) painter.DrawText(empty_text_, absolute_.Inset(10.0f, 0), TextRole::Body, theme.text_secondary);
    painter.PopClip();
    const bool hot = hovered_ || dragging_;
    painter.DrawScrollThumb(Thumb(std::max(expand_progress_, MaxScroll() > 0.5f ? 0.45f : 0.0f)),
                            hot ? theme.scrollbar_thumb_hover : theme.scrollbar_thumb);
}

} // namespace lumen

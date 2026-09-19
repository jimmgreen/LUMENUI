// lumen/LogView.h — 等宽虚拟化日志：贴底跟随、按级亮度、Ctrl+C 复制选中行。
// Events: OnViewChanged / BindViewChanged / OnFollowingChanged / BindFollowingChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Log.h"
#include "Signal.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace lumen {

struct LogEntry {
    std::wstring timestamp;
    LogLevel level = LogLevel::Info;
    std::wstring source;
    std::wstring message;
    std::wstring trace;
};

class LogView : public ControlOf<LogView> {
public:
    LogView& ItemCount(size_t count);
    size_t ItemCount() const noexcept { return item_count_; }
    // Providers use source indices. Increasing ItemCount appends; Refresh re-reads edited rows.
    LogView& LineText(std::function<void(size_t, std::wstring&)> provider);
    LogView& LineLevel(std::function<LogLevel(size_t)> provider);
    LogView& Entry(std::function<void(size_t, LogEntry&)> provider);
    LogView& Query(std::wstring_view query);
    const std::wstring& Query() const noexcept { return query_; }
    LogView& LevelEnabled(LogLevel level, bool enabled);
    bool LevelEnabled(LogLevel level) const noexcept;
    // Counts match the search query, before the level switches are applied.
    size_t LevelCount(LogLevel level) const noexcept;
    size_t VisibleCount() const noexcept { return visible_.size(); }
    size_t DataIndex(size_t view) const noexcept { return view < visible_.size() ? visible_[view] : item_count_; }
    LogView& Refresh();
    LogView& Follow(bool on = true);
    bool Follow() const noexcept { return follow_; }
    bool Following() const noexcept { return follow_ && following_; }
    LogView& EmptyText(std::wstring_view text) { empty_text_ = text; Invalidate(); return *this; }
    LogView& OnViewChanged(std::function<void()> handler) { view_changed_.Subscribe(std::move(handler)); return *this; }
    Connection BindViewChanged(std::function<void()> handler) { return view_changed_.Connect(std::move(handler)); }
    LogView& OnFollowingChanged(std::function<void(bool)> handler) { following_changed_.Subscribe(std::move(handler)); return *this; }
    Connection BindFollowingChanged(std::function<void(bool)> handler) { return following_changed_.Connect(std::move(handler)); }

    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    LogView& SelectedIndex(ptrdiff_t index);
    bool CopySelection() const;

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Prepare(Painter& painter, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    AutomationControlType AutomationType() const noexcept override { return AutomationControlType::List; }
    uint32_t AutomationPatterns() const noexcept override { return kPatternSelection | kPatternValue; }
    int AutomationItemCount() const noexcept override { return static_cast<int>(visible_.size()); }
    int AutomationSelectedIndex() const noexcept override { return static_cast<int>(selected_); }
    std::wstring AutomationItemName(int index) const override;
    std::wstring AutomationValue() const override { return AutomationItemName(static_cast<int>(selected_)); }
    bool AutomationSelectIndex(int index) override;

    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    bool CapturesOverlay(Point p) const override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    float RowHeight() const noexcept { return entry_ ? 26.0f : 20.0f; }
    float ContentHeight() const noexcept;
    float MaxScroll() const;
    void ClampScroll();
    void ScrollToEnd();
    void PauseFollowIfScrolled();
    ptrdiff_t RowAt(Point local) const;
    Rect VerticalTrack() const noexcept;
    ScrollThumb Thumb(float expand) const noexcept;
    bool BeginScrollDrag(Point local);

    void RebuildView(size_t begin = 0);
    void ReadEntry(size_t source, LogEntry& out) const;
    void UpdateFollowing(bool following);
    std::wstring FormatEntry(size_t source) const;
    struct Fields { Rect time, level, source, message, trace; };
    Fields EntryFields(float y, bool has_trace) const noexcept;
    std::function<void(size_t, LogEntry&)> entry_;
    std::vector<size_t> visible_;
    std::array<bool, 4> levels_{true, true, true, true};
    std::array<size_t, 4> level_counts_{};
    std::wstring query_;
    std::wstring empty_text_ = L"No matching log entries";
    std::vector<LogEntry> prepared_;
    size_t prepared_first_ = 0;
    Signal<> view_changed_;
    Signal<bool> following_changed_;
    size_t item_count_ = 0;
    bool follow_ = true;
    bool following_ = true;
    ptrdiff_t selected_ = -1;
    ptrdiff_t hover_row_ = -1;
    float scroll_offset_ = 0.0f;
    float target_offset_ = 0.0f;
    float expand_progress_ = 0.0f;
    bool dragging_ = false;
    float drag_grab_ = 0.0f;
    std::function<void(size_t, std::wstring&)> line_text_;
    std::function<LogLevel(size_t)> line_level_;
};

} // namespace lumen

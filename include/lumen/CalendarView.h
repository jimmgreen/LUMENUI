// lumen/CalendarView.h — 月历：日 / 月 / 年三层，点标题上钻。铬层与 DatePicker 弹层同一套 token。
// Events: OnDateChanged / BindDateChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <chrono>
#include <functional>
#include <optional>

namespace lumen {

class CalendarView : public ControlOf<CalendarView> {
public:
    using Date = std::chrono::year_month_day;

    CalendarView();

    const std::optional<Date>& Value() const noexcept { return value_; }
    CalendarView& Value(std::optional<Date> value);
    CalendarView& Range(std::optional<Date> minimum, std::optional<Date> maximum);
    CalendarView& OnDateChanged(std::function<void(std::optional<Date>)> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindDateChanged(std::function<void(std::optional<Date>)> handler) {
        return changed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;
    CursorShape CursorAt(Point local) const override;

private:
    enum class Mode { Days, Months, Years };

    void Commit(Date date);
    void MoveMonth(int delta);
    void SetMode(Mode mode);
    void ShiftYearPage(int delta);
    void ShiftMonthYear(int delta);
    void MoveYearCursor(int delta);
    void MoveMonthCursor(int delta);
    void SelectYear(int year_value);
    void SelectMonth(unsigned month_value);
    void RefreshTitle();
    Date ClampToRange(Date date) const;
    bool InRange(Date date) const;
    bool YearAllowed(int year_value) const;
    bool MonthAllowed(int year_value, unsigned month_value) const;
    void DrawDays(Painter& painter, const Theme& theme, float fade);
    void DrawMonths(Painter& painter, const Theme& theme, float fade);
    void DrawYears(Painter& painter, const Theme& theme, float fade);

    std::optional<Date> value_;
    std::optional<Date> minimum_;
    std::optional<Date> maximum_;
    std::chrono::year_month month_{};
    Date cursor_{};
    std::wstring month_title_;
    std::wstring months_title_;
    std::wstring year_title_;
    std::wstring weekday_[7];
    std::wstring month_name_[12];
    int first_day_ = 0;
    int year_page_start_ = 1970;
    int year_cursor_ = 1970;
    int month_year_ = 1970;
    int month_cursor_ = 1;
    int hover_day_ = -1;          // 1..31，-1 无
    int hover_month_ = -1;        // 1..12
    int hover_year_ = -1;
    Mode mode_ = Mode::Days;
    bool hover_title_ = false;
    float hover_t_ = 0.0f;
    float chevron_l_ = 0.0f;
    float chevron_r_ = 0.0f;
    float title_hover_t_ = 0.0f;
    float view_t_ = 0.0f;         // 0 日，1 月，2 年；EaseTo 过渡
    Signal<std::optional<Date>> changed_;
};
} // namespace lumen

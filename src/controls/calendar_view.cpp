#include "lumen/CalendarView.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace lumen {
namespace {

using Date = CalendarView::Date;
using namespace std::chrono;

sys_days ToDays(Date value) { return sys_days{value}; }

Date Today() {
    return year_month_day{floor<days>(system_clock::now())};
}

int FirstDayOfWeek() {
    wchar_t value[8]{};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK, value,
                        static_cast<int>(std::size(value))) <= 0) {
        return 0;
    }
    return (_wtoi(value) + 1) % 7;
}

Color Fade(Color c, float t) {
    c.a *= t;
    return c;
}

constexpr float kPad = 12.0f;
constexpr float kHeaderY = 8.0f;
constexpr float kHeaderH = 40.0f;
constexpr float kWeekH = 24.0f;
constexpr float kCellH = 34.0f;
constexpr float kChevronW = 52.0f;
constexpr float kYearPadX = 18.0f;
constexpr float kYearTop = 58.0f;
constexpr float kYearCellH = 54.0f;
constexpr int kFirstYear = 1;
constexpr int kLastYear = 9999;
constexpr int kLastPage = 9988;
constexpr uint32_t kMkLeft = 0x0001;

int PageStart(int year_value) {
    return std::clamp(year_value - 5, kFirstYear, kLastPage);
}

float LayerFade(float t, float center) {
    return Clamp(1.0f - std::fabs(t - center), 0.0f, 1.0f);
}

} // namespace

using std::chrono::day;
using std::chrono::days;
using std::chrono::last;
using std::chrono::months;
using std::chrono::weekday;
using std::chrono::year;
using std::chrono::year_month;
using std::chrono::January;
using std::chrono::December;

CalendarView::CalendarView() {
    cursor_ = Today();
    month_ = year_month{cursor_.year(), cursor_.month()};
    month_year_ = static_cast<int>(cursor_.year());
    month_cursor_ = static_cast<int>(static_cast<unsigned>(cursor_.month()));
    first_day_ = FirstDayOfWeek();
    for (int i = 0; i < 7; ++i) {
        const int chrono_day = (first_day_ + i) % 7;
        SYSTEMTIME st{};
        st.wYear = 2024;
        st.wMonth = 1;
        st.wDay = static_cast<WORD>(7 + chrono_day);   // 2024-01-07 is Sunday
        wchar_t name[8]{};
        if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd", name,
                            static_cast<int>(std::size(name)), nullptr) > 0) {
            weekday_[static_cast<size_t>(i)] = name;
        }
    }
    for (int m = 1; m <= 12; ++m) {
        SYSTEMTIME st{};
        st.wYear = 2024;
        st.wMonth = static_cast<WORD>(m);
        st.wDay = 1;
        wchar_t name[16]{};
        if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMM", name,
                            static_cast<int>(std::size(name)), nullptr) > 0) {
            month_name_[static_cast<size_t>(m - 1)] = name;
        } else {
            swprintf_s(name, L"%02d", m);
            month_name_[static_cast<size_t>(m - 1)] = name;
        }
    }
    year_cursor_ = month_year_;
    year_page_start_ = PageStart(year_cursor_);
    RefreshTitle();
}

CalendarView& CalendarView::Value(std::optional<Date> value) {
    if (value) value = ClampToRange(*value);
    if (value_ != value) {
        value_ = value;
        if (value_) {
            cursor_ = *value_;
            month_ = year_month{cursor_.year(), cursor_.month()};
        }
    }
    mode_ = Mode::Days;
    view_t_ = 0.0f;
    hover_year_ = -1;
    hover_month_ = -1;
    month_year_ = static_cast<int>(cursor_.year());
    month_cursor_ = static_cast<int>(static_cast<unsigned>(cursor_.month()));
    year_cursor_ = month_year_;
    year_page_start_ = PageStart(year_cursor_);
    RefreshTitle();
    Invalidate();
    return *this;
}

CalendarView& CalendarView::Range(std::optional<Date> minimum, std::optional<Date> maximum) {
    minimum_ = minimum;
    maximum_ = maximum;
    if (value_) Value(value_);
    Invalidate();
    return *this;
}

bool CalendarView::InRange(Date date) const {
    return (!minimum_ || ToDays(date) >= ToDays(*minimum_)) &&
           (!maximum_ || ToDays(date) <= ToDays(*maximum_));
}

Date CalendarView::ClampToRange(Date date) const {
    if (minimum_ && ToDays(date) < ToDays(*minimum_)) date = *minimum_;
    if (maximum_ && ToDays(date) > ToDays(*maximum_)) date = *maximum_;
    return date;
}

bool CalendarView::YearAllowed(int year_value) const {
    const Date first{year{year_value} / January / day{1}};
    const Date last_date{year{year_value} / December / day{31}};
    return (!minimum_ || ToDays(last_date) >= ToDays(*minimum_)) &&
           (!maximum_ || ToDays(first) <= ToDays(*maximum_));
}

bool CalendarView::MonthAllowed(int year_value, unsigned month_value) const {
    const auto ym = year{year_value} / std::chrono::month{month_value};
    const Date first{ym / day{1}};
    const Date last_date{ym / last};
    return (!minimum_ || ToDays(last_date) >= ToDays(*minimum_)) &&
           (!maximum_ || ToDays(first) <= ToDays(*maximum_));
}

void CalendarView::RefreshTitle() {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(static_cast<int>(month_.year()));
    st.wMonth = static_cast<WORD>(static_cast<unsigned>(month_.month()));
    st.wDay = 1;
    wchar_t text[64]{};
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMMM yyyy", text,
                        static_cast<int>(std::size(text)), nullptr) > 0) {
        month_title_ = text;
    } else {
        swprintf_s(text, L"%04d-%02u", static_cast<int>(month_.year()),
                   static_cast<unsigned>(month_.month()));
        month_title_ = text;
    }
    swprintf_s(text, L"%d", month_year_);
    months_title_ = text;
    swprintf_s(text, L"%d – %d", year_page_start_, year_page_start_ + 11);
    year_title_ = text;
}

void CalendarView::Commit(Date date) {
    date = ClampToRange(date);
    if (!InRange(date)) return;
    value_ = date;
    cursor_ = date;
    month_ = year_month{date.year(), date.month()};
    month_year_ = static_cast<int>(date.year());
    month_cursor_ = static_cast<int>(static_cast<unsigned>(date.month()));
    RefreshTitle();
    Invalidate();
    changed_.Emit(value_);
}

void CalendarView::MoveMonth(int delta) {
    year_month target = month_ + months{delta};
    const unsigned wanted = static_cast<unsigned>(cursor_.day());
    const unsigned last_day = static_cast<unsigned>(Date{target / last}.day());
    cursor_ = ClampToRange(Date{target / day{std::min(wanted, last_day)}});
    month_ = year_month{cursor_.year(), cursor_.month()};
    month_year_ = static_cast<int>(cursor_.year());
    month_cursor_ = static_cast<int>(static_cast<unsigned>(cursor_.month()));
    RefreshTitle();
    Invalidate();
}

void CalendarView::SetMode(Mode mode) {
    if (mode_ == mode) return;
    const Mode from = mode_;
    mode_ = mode;
    if (mode == Mode::Months && from == Mode::Days) {
        month_year_ = static_cast<int>(cursor_.year());
        month_cursor_ = static_cast<int>(static_cast<unsigned>(cursor_.month()));
    } else if (mode == Mode::Years) {
        year_cursor_ = month_year_;
        year_page_start_ = PageStart(year_cursor_);
    }
    hover_day_ = -1;
    hover_month_ = -1;
    hover_year_ = -1;
    RefreshTitle();
    if (window_) Animate();
    else view_t_ = static_cast<float>(mode);
    Invalidate();
}

void CalendarView::ShiftYearPage(int delta) {
    year_page_start_ = std::clamp(year_page_start_ + delta, kFirstYear, kLastPage);
    year_cursor_ = std::clamp(year_cursor_ + delta, year_page_start_, year_page_start_ + 11);
    RefreshTitle();
    Invalidate();
}

void CalendarView::ShiftMonthYear(int delta) {
    month_year_ = std::clamp(month_year_ + delta, kFirstYear, kLastYear);
    if (!YearAllowed(month_year_)) {
        month_year_ = std::clamp(month_year_ - delta, kFirstYear, kLastYear);
        return;
    }
    RefreshTitle();
    Invalidate();
}

void CalendarView::MoveYearCursor(int delta) {
    year_cursor_ = std::clamp(year_cursor_ + delta, kFirstYear, kLastYear);
    if (year_cursor_ < year_page_start_) {
        year_page_start_ = std::max(kFirstYear, year_page_start_ - 12);
    } else if (year_cursor_ > year_page_start_ + 11) {
        year_page_start_ = std::min(kLastPage, year_page_start_ + 12);
    }
    RefreshTitle();
    Invalidate();
}

void CalendarView::MoveMonthCursor(int delta) {
    int next = month_cursor_ + delta;
    while (next < 1) {
        if (month_year_ <= kFirstYear) {
            month_cursor_ = 1;
            RefreshTitle();
            Invalidate();
            return;
        }
        --month_year_;
        next += 12;
    }
    while (next > 12) {
        if (month_year_ >= kLastYear) {
            month_cursor_ = 12;
            RefreshTitle();
            Invalidate();
            return;
        }
        ++month_year_;
        next -= 12;
    }
    month_cursor_ = next;
    RefreshTitle();
    Invalidate();
}

void CalendarView::SelectYear(int year_value) {
    if (!YearAllowed(year_value)) return;
    month_year_ = year_value;
    year_cursor_ = year_value;
    const unsigned current = static_cast<unsigned>(cursor_.month());
    month_cursor_ = MonthAllowed(year_value, current) ? static_cast<int>(current) : 1;
    SetMode(Mode::Months);
}

void CalendarView::SelectMonth(unsigned month_value) {
    if (month_value < 1 || month_value > 12) return;
    if (!MonthAllowed(month_year_, month_value)) return;
    const year_month target{year{month_year_}, std::chrono::month{month_value}};
    const unsigned wanted = static_cast<unsigned>(cursor_.day());
    const unsigned max_day = static_cast<unsigned>(Date{target / last}.day());
    cursor_ = ClampToRange(Date{target / day{std::min(wanted, max_day)}});
    month_ = year_month{cursor_.year(), cursor_.month()};
    month_year_ = static_cast<int>(cursor_.year());
    month_cursor_ = static_cast<int>(static_cast<unsigned>(cursor_.month()));
    SetMode(Mode::Days);
}

Size CalendarView::Measure(Size available, const Theme&) {
    const float width = (available.w > 0.0f && available.w < 1.0e4f) ? std::min(available.w, 320.0f)
                                                                     : 308.0f;
    return {std::max(260.0f, width), kPad + kHeaderH + kWeekH + kCellH * 6.0f + kPad};
}

CursorShape CalendarView::CursorAt(Point) const { return CursorShape::Hand; }

void CalendarView::OnFocusChanged(bool focused) {
    focused_ = focused;
    Invalidate();
}

void CalendarView::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_day_ = -1;
    hover_month_ = -1;
    hover_year_ = -1;
    hover_title_ = false;
    Animate();
}

bool CalendarView::OnAnimate(float dt_seconds) {
    const float hover_target = mode_ == Mode::Years
                                   ? (hover_year_ >= 0 ? 1.0f : 0.0f)
                                   : mode_ == Mode::Months ? (hover_month_ > 0 ? 1.0f : 0.0f)
                                                           : (hover_day_ > 0 ? 1.0f : 0.0f);
    bool active = EaseTo(hover_t_, hover_target, dt_seconds);
    const bool in_header = hovered_ && mouse_local_.y >= kHeaderY &&
                           mouse_local_.y <= kHeaderY + kHeaderH;
    const float left_t = (in_header && mouse_local_.x <= kChevronW) ? 1.0f : 0.0f;
    const float right_t = (in_header && mouse_local_.x >= absolute_.w - kChevronW) ? 1.0f : 0.0f;
    active |= EaseTo(chevron_l_, left_t, dt_seconds);
    active |= EaseTo(chevron_r_, right_t, dt_seconds);
    active |= EaseTo(title_hover_t_, hover_title_ ? 1.0f : 0.0f, dt_seconds);
    active |= EaseTo(view_t_, static_cast<float>(mode_), dt_seconds, 14.0f);
    return active || Control::OnAnimate(dt_seconds);
}

void CalendarView::DrawDays(Painter& painter, const Theme& theme, float fade) {
    if (fade < 0.008f) return;
    const float left = absolute_.x + 14.0f;
    const float top = absolute_.y + 54.0f;
    const float cell_w = (absolute_.w - 28.0f) / 7.0f;
    for (int col = 0; col < 7; ++col) {
        painter.DrawText(weekday_[static_cast<size_t>(col)],
                         {left + static_cast<float>(col) * cell_w, top, cell_w, kWeekH},
                         TextRole::Caption, Fade(theme.text_secondary, fade), Align::Center);
    }
    const Date first{month_ / day{1}};
    const int leading = (weekday{ToDays(first)}.c_encoding() - first_day_ + 7) % 7;
    const unsigned count = static_cast<unsigned>(Date{month_ / last}.day());
    const Date today = Today();
    for (unsigned day_number = 1; day_number <= count; ++day_number) {
        const int slot = leading + static_cast<int>(day_number) - 1;
        const int row = slot / 7;
        const int col = slot % 7;
        const Date date{month_ / day{day_number}};
        const Rect cell{left + static_cast<float>(col) * cell_w,
                        top + kWeekH + static_cast<float>(row) * kCellH, cell_w, kCellH};
        const Rect slot_r = cell.Inset(3.0f, 3.0f);
        const bool allowed = InRange(date);
        const bool selected = value_ && ToDays(*value_) == ToDays(date);
        const bool cursor = ToDays(cursor_) == ToDays(date);
        const bool today_cell = ToDays(today) == ToDays(date);
        if (selected) {
            painter.FillRoundedRect(slot_r, 8.0f, Fade(theme.accent, fade));
        } else if (static_cast<int>(day_number) == hover_day_ && allowed) {
            Color hover = theme.fill_hover;
            hover.a *= Lerp(0.35f, 1.0f, hover_t_) * fade;
            painter.FillRoundedRect(slot_r, 8.0f, hover);
        } else if (cursor) {
            painter.FillRoundedRect(slot_r, 8.0f, Fade(theme.fill_hover, fade));
        }
        wchar_t number[4]{};
        swprintf_s(number, L"%u", day_number);
        const Color ink = !allowed ? theme.text_disabled
                                   : selected ? theme.accent_text : theme.text;
        painter.DrawText(number, cell, TextRole::Body, Fade(ink, fade), Align::Center);
        if (today_cell && !selected) {
            painter.FillRoundedRect(
                {slot_r.x + slot_r.w * 0.5f - 6.0f, slot_r.Bottom() - 3.0f, 12.0f, 2.0f}, 1.0f,
                Fade(theme.accent, fade));
        }
        if (cursor && FocusVisible()) {
            painter.DrawFocusRing(slot_r, 8.0f, Fade(theme.accent, fade), theme.focus_ring_width);
        }
    }
}

void CalendarView::DrawMonths(Painter& painter, const Theme& theme, float fade) {
    if (fade < 0.008f) return;
    const float left = absolute_.x + kYearPadX;
    const float top = absolute_.y + kYearTop;
    const float cell_w = (absolute_.w - kYearPadX * 2.0f) / 3.0f;
    for (int slot = 0; slot < 12; ++slot) {
        const unsigned month_number = static_cast<unsigned>(slot + 1);
        const Rect cell{left + static_cast<float>(slot % 3) * cell_w,
                        top + static_cast<float>(slot / 3) * kYearCellH, cell_w, kYearCellH};
        const Rect slot_r = cell.Inset(5.0f, 7.0f);
        const bool allowed = MonthAllowed(month_year_, month_number);
        const bool selected = value_ && static_cast<int>(value_->year()) == month_year_ &&
                              static_cast<unsigned>(value_->month()) == month_number;
        const bool cursor = month_cursor_ == static_cast<int>(month_number);
        if (selected) {
            painter.FillRoundedRect(slot_r, 8.0f, Fade(theme.accent, fade));
        } else if (static_cast<int>(month_number) == hover_month_ && allowed) {
            Color hover = theme.fill_hover;
            hover.a *= Lerp(0.35f, 1.0f, hover_t_) * fade;
            painter.FillRoundedRect(slot_r, 8.0f, hover);
        } else if (cursor) {
            painter.FillRoundedRect(slot_r, 8.0f, Fade(theme.fill_selected, fade));
        }
        const Color ink = !allowed ? theme.text_disabled
                                   : selected ? theme.accent_text : theme.text;
        painter.DrawText(month_name_[static_cast<size_t>(slot)], cell, TextRole::Body,
                         Fade(ink, fade), Align::Center);
        if (cursor && FocusVisible()) {
            painter.DrawFocusRing(slot_r, 8.0f, Fade(theme.accent, fade), theme.focus_ring_width);
        }
    }
}

void CalendarView::DrawYears(Painter& painter, const Theme& theme, float fade) {
    if (fade < 0.008f) return;
    const float left = absolute_.x + kYearPadX;
    const float top = absolute_.y + kYearTop;
    const float cell_w = (absolute_.w - kYearPadX * 2.0f) / 3.0f;
    for (int slot = 0; slot < 12; ++slot) {
        const int year_number = year_page_start_ + slot;
        const Rect cell{left + static_cast<float>(slot % 3) * cell_w,
                        top + static_cast<float>(slot / 3) * kYearCellH, cell_w, kYearCellH};
        const Rect slot_r = cell.Inset(5.0f, 7.0f);
        const bool allowed = YearAllowed(year_number);
        const bool selected = value_ && static_cast<int>(value_->year()) == year_number;
        const bool cursor = year_number == year_cursor_;
        if (selected) {
            painter.FillRoundedRect(slot_r, 8.0f, Fade(theme.accent, fade));
        } else if (year_number == hover_year_ && allowed) {
            Color hover = theme.fill_hover;
            hover.a *= Lerp(0.35f, 1.0f, hover_t_) * fade;
            painter.FillRoundedRect(slot_r, 8.0f, hover);
        } else if (cursor) {
            painter.FillRoundedRect(slot_r, 8.0f, Fade(theme.fill_selected, fade));
        }
        wchar_t text[8]{};
        swprintf_s(text, L"%d", year_number);
        const Color ink = !allowed ? theme.text_disabled
                                   : selected ? theme.accent_text : theme.text;
        painter.DrawText(text, cell, TextRole::Body, Fade(ink, fade), Align::Center);
        if (cursor && FocusVisible()) {
            painter.DrawFocusRing(slot_r, 8.0f, Fade(theme.accent, fade), theme.focus_ring_width);
        }
    }
}

void CalendarView::Draw(Painter& painter, const Theme& theme) {
    painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.bg);
    painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.fill_input);
    painter.StrokeRoundedRect(absolute_, theme.radius_flyout, theme.stroke_card);
    painter.DrawInnerLight(absolute_, theme.radius_flyout, theme.specular_line,
                           Color{0.0f, 0.0f, 0.0f, 0.4f});
    const Rect header{absolute_.x + kPad, absolute_.y + kHeaderY, absolute_.w - kPad * 2.0f,
                      kHeaderH};
    const Rect title_r{header.x + 40.0f, header.y, header.w - 80.0f, header.h};
    if (title_hover_t_ > 0.01f) {
        Color wash = theme.fill_hover;
        wash.a *= title_hover_t_;
        painter.FillRoundedRect(title_r.Inset(0.0f, 4.0f), 8.0f, wash);
    }
    Color chev_l = theme.text_secondary;
    chev_l.a = Lerp(theme.text_secondary.a, theme.text.a, chevron_l_);
    Color chev_r = theme.text_secondary;
    chev_r.a = Lerp(theme.text_secondary.a, theme.text.a, chevron_r_);
    painter.DrawIcon(icon::kChevronLeft, {header.x, header.y, 36.0f, header.h}, 10.0f, chev_l);
    painter.DrawIcon(icon::kChevronRight, {header.Right() - 36.0f, header.y, 36.0f, header.h}, 10.0f,
                     chev_r);
    const float days_a = LayerFade(view_t_, 0.0f);
    const float months_a = LayerFade(view_t_, 1.0f);
    const float years_a = LayerFade(view_t_, 2.0f);
    if (days_a > 0.005f) {
        painter.DrawText(month_title_, title_r, TextRole::BodyStrong, Fade(theme.text, days_a),
                         Align::Center);
    }
    if (months_a > 0.005f) {
        painter.DrawText(months_title_, title_r, TextRole::BodyStrong, Fade(theme.text, months_a),
                         Align::Center);
    }
    if (years_a > 0.005f) {
        painter.DrawText(year_title_, title_r, TextRole::BodyStrong, Fade(theme.text, years_a),
                         Align::Center);
    }

    const Rect body{absolute_.x, header.Bottom(), absolute_.w,
                    std::max(0.0f, absolute_.Bottom() - header.Bottom())};
    if (body.IsEmpty()) return;
    painter.PushClip(body);
    const Point origin{body.x + body.w * 0.5f, body.y + body.h * 0.5f};
    if (days_a > 0.005f) {
        const float scale = Lerp(1.0f, 1.08f, Clamp(view_t_, 0.0f, 1.0f));
        painter.PushScale(origin, scale, scale);
        DrawDays(painter, theme, days_a);
        painter.PopTransform();
    }
    if (months_a > 0.005f) {
        const float scale = view_t_ <= 1.0f ? Lerp(0.92f, 1.0f, view_t_)
                                            : Lerp(1.0f, 1.08f, view_t_ - 1.0f);
        painter.PushScale(origin, scale, scale);
        DrawMonths(painter, theme, months_a);
        painter.PopTransform();
    }
    if (years_a > 0.005f) {
        const float scale = Lerp(0.92f, 1.0f, Clamp(view_t_ - 1.0f, 0.0f, 1.0f));
        painter.PushScale(origin, scale, scale);
        DrawYears(painter, theme, years_a);
        painter.PopTransform();
    }
    painter.PopClip();
}

bool CalendarView::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (mode_ == Mode::Years) {
        if (vk == VK_ESCAPE) {
            SetMode(Mode::Months);
            return true;
        }
        if (vk == VK_RETURN || vk == VK_SPACE) {
            SelectYear(year_cursor_);
            return true;
        }
        int delta = 0;
        if (vk == VK_LEFT) delta = -1;
        else if (vk == VK_RIGHT) delta = 1;
        else if (vk == VK_UP) delta = -3;
        else if (vk == VK_DOWN) delta = 3;
        else if (vk == VK_PRIOR) delta = -12;
        else if (vk == VK_NEXT) delta = 12;
        if (delta != 0) {
            MoveYearCursor(delta);
            return true;
        }
        return false;
    }
    if (mode_ == Mode::Months) {
        if (vk == VK_ESCAPE) {
            SetMode(Mode::Days);
            return true;
        }
        if (vk == VK_RETURN || vk == VK_SPACE) {
            SelectMonth(static_cast<unsigned>(month_cursor_));
            return true;
        }
        int delta = 0;
        if (vk == VK_LEFT) delta = -1;
        else if (vk == VK_RIGHT) delta = 1;
        else if (vk == VK_UP) delta = -3;
        else if (vk == VK_DOWN) delta = 3;
        else if (vk == VK_PRIOR) delta = -12;
        else if (vk == VK_NEXT) delta = 12;
        if (delta != 0) {
            MoveMonthCursor(delta);
            return true;
        }
        return false;
    }
    if (vk == VK_RETURN || vk == VK_SPACE) {
        Commit(cursor_);
        return true;
    }
    int delta = 0;
    if (vk == VK_LEFT) delta = -1;
    else if (vk == VK_RIGHT) delta = 1;
    else if (vk == VK_UP) delta = -7;
    else if (vk == VK_DOWN) delta = 7;
    if (delta != 0) {
        cursor_ = ClampToRange(Date{ToDays(cursor_) + days{delta}});
        month_ = year_month{cursor_.year(), cursor_.month()};
        month_year_ = static_cast<int>(cursor_.year());
        month_cursor_ = static_cast<int>(static_cast<unsigned>(cursor_.month()));
        RefreshTitle();
        Invalidate();
        return true;
    }
    if (vk == VK_PRIOR || vk == VK_NEXT) {
        MoveMonth(vk == VK_PRIOR ? -1 : 1);
        return true;
    }
    return false;
}

void CalendarView::OnMouseMove(Point local, uint32_t) {
    if (!enabled_) return;
    const bool title = local.y >= kHeaderY && local.y <= kHeaderY + kHeaderH &&
                       local.x > kChevronW && local.x < absolute_.w - kChevronW;
    int next_day = -1;
    int next_month = -1;
    int next_year = -1;
    if (mode_ == Mode::Years) {
        const float cell_w = (absolute_.w - kYearPadX * 2.0f) / 3.0f;
        if (local.x >= kYearPadX && local.x < absolute_.w - kYearPadX && local.y >= kYearTop) {
            const int col = static_cast<int>((local.x - kYearPadX) / cell_w);
            const int row = static_cast<int>((local.y - kYearTop) / kYearCellH);
            if (col >= 0 && col < 3 && row >= 0 && row < 4) {
                next_year = year_page_start_ + row * 3 + col;
            }
        }
    } else if (mode_ == Mode::Months) {
        const float cell_w = (absolute_.w - kYearPadX * 2.0f) / 3.0f;
        if (local.x >= kYearPadX && local.x < absolute_.w - kYearPadX && local.y >= kYearTop) {
            const int col = static_cast<int>((local.x - kYearPadX) / cell_w);
            const int row = static_cast<int>((local.y - kYearTop) / kYearCellH);
            if (col >= 0 && col < 3 && row >= 0 && row < 4) {
                next_month = row * 3 + col + 1;
            }
        }
    } else {
        const float cell_w = (absolute_.w - 28.0f) / 7.0f;
        const float y = local.y - 78.0f;
        if (local.x >= 14.0f && local.x < absolute_.w - 14.0f && y >= 0.0f) {
            const int col = static_cast<int>((local.x - 14.0f) / cell_w);
            const int row = static_cast<int>(y / kCellH);
            if (col >= 0 && col < 7 && row >= 0 && row < 6) {
                const Date first{month_ / day{1}};
                const int leading = (weekday{ToDays(first)}.c_encoding() - first_day_ + 7) % 7;
                const int number = row * 7 + col - leading + 1;
                const int count = static_cast<int>(static_cast<unsigned>(Date{month_ / last}.day()));
                if (number >= 1 && number <= count) next_day = number;
            }
        }
    }
    if (title != hover_title_ || next_day != hover_day_ || next_month != hover_month_ ||
        next_year != hover_year_) {
        hover_title_ = title;
        hover_day_ = next_day;
        hover_month_ = next_month;
        hover_year_ = next_year;
        Animate();
        Invalidate();
    }
}

void CalendarView::OnMouseDown(Point local, uint32_t buttons) {
    if (!enabled_ || !(buttons & kMkLeft)) return;
    Focus();
    if (local.y >= kHeaderY && local.y <= kHeaderY + kHeaderH) {
        if (local.x <= kChevronW) {
            if (mode_ == Mode::Years) ShiftYearPage(-12);
            else if (mode_ == Mode::Months) ShiftMonthYear(-1);
            else MoveMonth(-1);
        } else if (local.x >= absolute_.w - kChevronW) {
            if (mode_ == Mode::Years) ShiftYearPage(12);
            else if (mode_ == Mode::Months) ShiftMonthYear(1);
            else MoveMonth(1);
        } else if (mode_ == Mode::Days) {
            SetMode(Mode::Months);
        } else if (mode_ == Mode::Months) {
            SetMode(Mode::Years);
        }
        return;
    }
    if (mode_ == Mode::Years) {
        const float cell_w = (absolute_.w - kYearPadX * 2.0f) / 3.0f;
        if (local.x < kYearPadX || local.x >= absolute_.w - kYearPadX || local.y < kYearTop) return;
        const int col = static_cast<int>((local.x - kYearPadX) / cell_w);
        const int row = static_cast<int>((local.y - kYearTop) / kYearCellH);
        if (col < 0 || col >= 3 || row < 0 || row >= 4) return;
        SelectYear(year_page_start_ + row * 3 + col);
        return;
    }
    if (mode_ == Mode::Months) {
        const float cell_w = (absolute_.w - kYearPadX * 2.0f) / 3.0f;
        if (local.x < kYearPadX || local.x >= absolute_.w - kYearPadX || local.y < kYearTop) return;
        const int col = static_cast<int>((local.x - kYearPadX) / cell_w);
        const int row = static_cast<int>((local.y - kYearTop) / kYearCellH);
        if (col < 0 || col >= 3 || row < 0 || row >= 4) return;
        SelectMonth(static_cast<unsigned>(row * 3 + col + 1));
        return;
    }
    const float cell_w = (absolute_.w - 28.0f) / 7.0f;
    const float y = local.y - 78.0f;
    if (local.x < 14.0f || local.x >= absolute_.w - 14.0f || y < 0.0f) return;
    const int col = static_cast<int>((local.x - 14.0f) / cell_w);
    const int row = static_cast<int>(y / kCellH);
    if (col < 0 || col >= 7 || row < 0 || row >= 6) return;
    const Date first{month_ / day{1}};
    const int leading = (weekday{ToDays(first)}.c_encoding() - first_day_ + 7) % 7;
    const int number = row * 7 + col - leading + 1;
    const int count = static_cast<int>(static_cast<unsigned>(Date{month_ / last}.day()));
    if (number < 1 || number > count) return;
    Commit(Date{month_ / day{static_cast<unsigned>(number)}});
}

} // namespace lumen

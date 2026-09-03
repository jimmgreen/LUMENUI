#include "lumen/DatePicker.h"
#include "lumen/CalendarView.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <string>

namespace lumen {
namespace {

using Date = DatePicker::Date;
using namespace std::chrono;

sys_days ToDays(Date value) { return sys_days{value}; }

Date ClampDate(Date value, const std::optional<Date>& minimum,
               const std::optional<Date>& maximum) {
    if (minimum && ToDays(value) < ToDays(*minimum)) value = *minimum;
    if (maximum && ToDays(value) > ToDays(*maximum)) value = *maximum;
    return value;
}

std::wstring FormatDate(Date value) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(static_cast<int>(value.year()));
    st.wMonth = static_cast<WORD>(static_cast<unsigned>(value.month()));
    st.wDay = static_cast<WORD>(static_cast<unsigned>(value.day()));
    wchar_t text[128]{};
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_AUTOLAYOUT, &st, nullptr, text,
                        static_cast<int>(std::size(text)), nullptr) > 0) {
        return text;
    }
    swprintf_s(text, L"%04d-%02u-%02u", static_cast<int>(value.year()),
               static_cast<unsigned>(value.month()), static_cast<unsigned>(value.day()));
    return text;
}

} // namespace

struct DatePicker::Impl {
    explicit Impl(DatePicker* control) : owner(control) {
        calendar.OnDateChanged([this](std::optional<Date> date) {
            if (!date) return;
            value = date;
            display = FormatDate(*date);
            owner->Invalidate();
            changed.Emit(value);
            if (owner->window_) WindowImpl::CloseTransient(owner->window_);
        });
    }

    DatePicker* owner = nullptr;
    CalendarView calendar;
    std::optional<Date> value;
    std::optional<Date> minimum;
    std::optional<Date> maximum;
    std::wstring placeholder{L"Select date"};
    std::wstring display;
    Signal<std::optional<Date>> changed;
    ScopedConnection value_prop;
    ScopedConnection value_ctrl;
    bool bind_loop = false;
};

DatePicker::DatePicker() : impl_(std::make_unique<Impl>(this)) {}

DatePicker::~DatePicker() {
    if (window_ && WindowImpl::TransientActive(window_, &impl_->calendar)) {
        WindowImpl::CloseTransient(window_);
    }
}

const std::optional<DatePicker::Date>& DatePicker::Value() const noexcept { return impl_->value; }

DatePicker& DatePicker::Value(std::optional<Date> value) {
    if (value && !value->ok()) value.reset();
    if (value) value = ClampDate(*value, impl_->minimum, impl_->maximum);
    impl_->value = value;
    impl_->display = value ? FormatDate(*value) : std::wstring{};
    impl_->calendar.Value(value);
    Invalidate();
    return *this;
}

DatePicker& DatePicker::Range(std::optional<Date> minimum, std::optional<Date> maximum) {
    if (minimum && !minimum->ok()) minimum.reset();
    if (maximum && !maximum->ok()) maximum.reset();
    if (minimum && maximum && ToDays(*minimum) > ToDays(*maximum)) std::swap(minimum, maximum);
    impl_->minimum = minimum;
    impl_->maximum = maximum;
    impl_->calendar.Range(minimum, maximum);
    if (impl_->value) Value(impl_->value);
    return *this;
}

DatePicker& DatePicker::Placeholder(std::wstring_view value) {
    impl_->placeholder.assign(value);
    Invalidate();
    return *this;
}

DatePicker& DatePicker::OnValueChanged(std::function<void(std::optional<Date>)> handler) {
    impl_->changed.Subscribe(std::move(handler));
    return *this;
}
Connection DatePicker::BindValueChanged(std::function<void(std::optional<Date>)> handler) {
    return impl_->changed.Connect(std::move(handler));
}

Size DatePicker::Measure(Size, const Theme& theme) { return {176.0f, theme.input_height}; }

void DatePicker::Draw(Painter& painter, const Theme& theme) {
    Color fill = !enabled_ ? theme.fill_input_disabled
                          : focused_ ? theme.fill_input_focus
                                     : hovered_ ? theme.fill_input_hover : theme.fill_input;
    painter.FillRoundedRect(absolute_, theme.radius_control, fill);
    painter.DrawInnerLight(absolute_, theme.radius_control, theme.edge_light,
                           Color{0.0f, 0.0f, 0.0f, 0.35f});
    painter.StrokeRoundedRect(absolute_, theme.radius_control,
                              focused_ ? theme.accent : theme.control_stroke);
    const std::wstring& text = impl_->value ? impl_->display : impl_->placeholder;
    painter.DrawText(text, {absolute_.x + 12.0f, absolute_.y, absolute_.w - 48.0f, absolute_.h},
                     TextRole::Body,
                     !enabled_ ? theme.text_disabled
                               : impl_->value ? theme.text : theme.text_secondary);
    painter.DrawIcon(icon::kCalendar, {absolute_.Right() - 40.0f, absolute_.y, 40.0f, absolute_.h},
                     16.0f, enabled_ ? theme.text_secondary : theme.text_disabled);
    if (focused_) PaintFocusRing(painter, theme, absolute_, theme.radius_control);
}

void DatePicker::OpenPopup() {
    if (!window_ || !enabled_) return;
    Date initial = impl_->value.value_or(Date{floor<days>(system_clock::now())});
    initial = ClampDate(initial, impl_->minimum, impl_->maximum);
    impl_->calendar.Range(impl_->minimum, impl_->maximum);
    impl_->calendar.Value(initial);
    WindowImpl::ShowTransient(window_, &impl_->calendar, this, 308.0f, false);
}

bool DatePicker::OnKey(uint32_t vk) {
    if (vk == VK_RETURN || vk == VK_SPACE || vk == VK_DOWN) {
        OpenPopup();
        return true;
    }
    if (vk == VK_DELETE && impl_->value) {
        impl_->value.reset();
        impl_->display.clear();
        impl_->calendar.Value(std::nullopt);
        impl_->changed.Emit(impl_->value);
        Invalidate();
        return true;
    }
    return false;
}

void DatePicker::OnMouseDown(Point, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    OpenPopup();
}

void DatePicker::OnFocusChanged(bool focused) {
    focused_ = focused;
    Invalidate();
}

CursorShape DatePicker::CursorAt(Point) const { return CursorShape::Hand; }

DatePicker& DatePicker::BindValue(Property<std::optional<Date>>& p) {
    auto apply = [this, &p] {
        if (impl_->bind_loop) return;
        impl_->bind_loop = true;
        Value(p.Get());
        impl_->bind_loop = false;
    };
    apply();
    impl_->value_prop = ScopedConnection(p.OnChanged([apply](const std::optional<Date>&) { apply(); }));
    impl_->value_ctrl = ScopedConnection(impl_->changed.Connect([this, &p](std::optional<Date> v) {
        if (impl_->bind_loop) return;
        impl_->bind_loop = true;
        p = std::move(v);
        impl_->bind_loop = false;
    }));
    return *this;
}

} // namespace lumen

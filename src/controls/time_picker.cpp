#include "lumen/TimePicker.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <string>

namespace lumen {
namespace {

using Time = TimePicker::Time;

int MinuteCount(Time value) { return static_cast<int>(value.count()); }

Time ClampTime(Time value, const std::optional<Time>& minimum,
               const std::optional<Time>& maximum) {
    int minutes = std::clamp(MinuteCount(value), 0, 1439);
    if (minimum) minutes = std::max(minutes, MinuteCount(*minimum));
    if (maximum) minutes = std::min(minutes, MinuteCount(*maximum));
    return Time{minutes};
}

bool Uses24HourClock() {
    wchar_t value[8]{};
    return GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_ITIME, value,
                           static_cast<int>(std::size(value))) > 0 && value[0] == L'1';
}

bool InRange(Time value, const std::optional<Time>& minimum,
             const std::optional<Time>& maximum) {
    return (!minimum || value >= *minimum) && (!maximum || value <= *maximum);
}

int Wrap(int value, int count) {
    const int wrapped = value % count;
    return wrapped < 0 ? wrapped + count : wrapped;
}

bool Resolve24Hour(TimeDisplayMode mode) {
    if (mode == TimeDisplayMode::TwentyFourHour) return true;
    if (mode == TimeDisplayMode::TwelveHour) return false;
    return Uses24HourClock();
}

std::wstring FormatTime(Time value, TimeDisplayMode mode) {
    const int total = std::clamp(MinuteCount(value), 0, 1439);
    const int hour = total / 60;
    const int minute = total % 60;
    wchar_t text[24]{};
    if (mode == TimeDisplayMode::System) {
        SYSTEMTIME st{};
        st.wHour = static_cast<WORD>(hour);
        st.wMinute = static_cast<WORD>(minute);
        if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, text,
                            static_cast<int>(std::size(text))) > 0) {
            return text;
        }
    }
    if (Resolve24Hour(mode)) {
        swprintf_s(text, L"%02d:%02d", hour, minute);
    } else {
        swprintf_s(text, L"%02d:%02d %ls", hour % 12 == 0 ? 12 : hour % 12, minute,
                   hour < 12 ? L"AM" : L"PM");
    }
    return text;
}

} // namespace

struct TimePicker::Impl {
    class TimePopup : public Control {
    public:
        explicit TimePopup(Impl* owner) : owner_(owner) {}

        void OpenAt(Time value) {
            cursor_ = ClampTime(value, owner_->minimum, owner_->maximum);
            selected_column_ = 0;
        }

    protected:
        Size Measure(Size, const Theme&) override { return {280.0f, 286.0f}; }
        bool Focusable() const noexcept override { return true; }
        CursorShape CursorAt(Point) const override { return CursorShape::Hand; }

        void Draw(Painter& painter, const Theme& theme) override {
            painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.bg);
            painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.fill_input);
            painter.StrokeRoundedRect(absolute_, theme.radius_flyout, theme.stroke_card);
            painter.DrawInnerLight(absolute_, theme.radius_flyout, theme.specular_line,
                                   Color{0.0f, 0.0f, 0.0f, 0.4f});
            painter.DrawText(L"Hour", {absolute_.x + 16.0f, absolute_.y + 8.0f, 112.0f, 28.0f},
                             TextRole::CaptionStrong, theme.text_secondary, Align::Center);
            painter.DrawText(L"Minute", {absolute_.x + 152.0f, absolute_.y + 8.0f, 112.0f, 28.0f},
                             TextRole::CaptionStrong, theme.text_secondary, Align::Center);
            const int total = MinuteCount(cursor_);
            const int hour = total / 60;
            const int minute = total % 60;
            constexpr float kRowH = 36.0f;
            for (int row = -2; row <= 2; ++row) {
                const Rect hour_cell{absolute_.x + 16.0f, absolute_.y + 40.0f + (row + 2) * kRowH,
                                     112.0f, kRowH};
                const Rect minute_cell{absolute_.x + 152.0f,
                                       absolute_.y + 40.0f + (row + 2) * kRowH, 112.0f, kRowH};
                if (row == 0) {
                    painter.FillRoundedRect(hour_cell.Inset(4.0f, 2.0f), 8.0f,
                                            selected_column_ == 0 ? theme.fill_selected
                                                                  : theme.fill_hover);
                    painter.FillRoundedRect(minute_cell.Inset(4.0f, 2.0f), 8.0f,
                                            selected_column_ == 1 ? theme.fill_selected
                                                                  : theme.fill_hover);
                }
                const int h = (hour + row + 24) % 24;
                const int minute_steps = (59 / owner_->increment) + 1;
                const int current_step = minute / owner_->increment;
                const int m = ((current_step + row + minute_steps) % minute_steps) * owner_->increment;
                wchar_t hour_text[24]{};
                if (owner_->use_24_hour) swprintf_s(hour_text, L"%02d", h);
                else swprintf_s(hour_text, L"%d %s", h % 12 == 0 ? 12 : h % 12,
                                h < 12 ? L"AM" : L"PM");
                wchar_t minute_text[8]{};
                swprintf_s(minute_text, L"%02d", m);
                const Color color = row == 0 ? theme.text : theme.text_secondary;
                painter.DrawText(hour_text, hour_cell, TextRole::Body, color, Align::Center);
                painter.DrawText(minute_text, minute_cell, TextRole::Body, color, Align::Center);
            }
            const Rect done{absolute_.x + 16.0f, absolute_.Bottom() - 50.0f,
                            absolute_.w - 32.0f, 38.0f};
            painter.FillRoundedRect(done, theme.radius_control, theme.accent);
            painter.DrawText(L"Done", done, TextRole::BodyStrong, theme.accent_text, Align::Center);
            if (HasFocus()) {
                const Rect focus = selected_column_ == 0
                                       ? Rect{absolute_.x + 20.0f, absolute_.y + 112.0f, 104.0f, 32.0f}
                                       : Rect{absolute_.x + 156.0f, absolute_.y + 112.0f, 104.0f, 32.0f};
                PaintFocusRing(painter, theme, focus, 8.0f);
            }
        }

        bool OnKey(uint32_t vk) override {
            if (vk == VK_ESCAPE) {
                WindowImpl::CloseTransient(window_);
                return true;
            }
            if (vk == VK_RETURN || vk == VK_SPACE) {
                Commit();
                return true;
            }
            if (vk == VK_LEFT || vk == VK_RIGHT) {
                selected_column_ = vk == VK_LEFT ? 0 : 1;
                Invalidate();
                return true;
            }
            if (vk == VK_UP || vk == VK_DOWN) {
                Step(vk == VK_UP ? -1 : 1);
                return true;
            }
            if (vk == VK_HOME || vk == VK_END) {
                cursor_ = ClampTime(vk == VK_HOME ? owner_->minimum.value_or(Time{0})
                                                  : owner_->maximum.value_or(Time{1439}),
                                    owner_->minimum, owner_->maximum);
                Invalidate();
                return true;
            }
            return false;
        }

        bool OnWheel(float delta) override {
            Step(delta > 0.0f ? -1 : 1);
            return true;
        }

        void OnMouseMove(Point local, uint32_t) override {
            const int next = local.x < absolute_.w * 0.5f ? 0 : 1;
            if (next != selected_column_) {
                selected_column_ = next;
                Invalidate();
            }
        }

        void OnMouseDown(Point local, uint32_t buttons) override {
            if (!(buttons & 0x0001)) return;
            Focus();
            if (local.y >= absolute_.h - 54.0f) {
                Commit();
                return;
            }
            if (local.y < 40.0f || local.y >= 220.0f) return;
            selected_column_ = local.x < absolute_.w * 0.5f ? 0 : 1;
            const int row = static_cast<int>((local.y - 40.0f) / 36.0f) - 2;
            if (row != 0) Step(row);
            else Invalidate();
        }

    private:
        void Step(int amount) {
            const int direction = amount < 0 ? -1 : 1;
            const int repeats = std::abs(amount);
            const int option_count = selected_column_ == 0 ? 24 : (59 / owner_->increment) + 1;
            for (int repeat = 0; repeat < repeats; ++repeat) {
                const int total = MinuteCount(cursor_);
                const int hour = total / 60;
                const int minute = total % 60;
                bool moved = false;
                for (int offset = 1; offset <= option_count; ++offset) {
                    Time candidate{};
                    if (selected_column_ == 0) {
                        candidate = Time{Wrap(hour + direction * offset, 24) * 60 + minute};
                    } else {
                        const int step = minute / owner_->increment;
                        const int next = Wrap(step + direction * offset, option_count) *
                                         owner_->increment;
                        candidate = Time{hour * 60 + next};
                    }
                    if (InRange(candidate, owner_->minimum, owner_->maximum)) {
                        cursor_ = candidate;
                        moved = true;
                        break;
                    }
                }
                if (!moved) break;
            }
            Invalidate();
        }
        void Commit() {
            owner_->value = cursor_;
            owner_->display = FormatTime(cursor_, owner_->display_mode);
            owner_->changed.Emit(owner_->value);
            Invalidate();
            WindowImpl::CloseTransient(window_);
        }

        Impl* owner_ = nullptr;
        Time cursor_{0};
        int selected_column_ = 0;
    };

    explicit Impl(TimePicker* control) : owner(control), popup(this) {}
    TimePicker* owner = nullptr;
    std::optional<Time> value;
    std::optional<Time> minimum;
    std::optional<Time> maximum;
    int increment = 1;
    TimeDisplayMode display_mode = TimeDisplayMode::System;
    bool use_24_hour = Uses24HourClock();
    std::wstring placeholder{L"Select time"};
    std::wstring display;
    Signal<std::optional<Time>> changed;
    TimePopup popup;
    ScopedConnection value_prop;
    ScopedConnection value_ctrl;
    bool bind_loop = false;
};

TimePicker::TimePicker() : impl_(std::make_unique<Impl>(this)) {}

TimePicker::~TimePicker() {
    if (window_ && WindowImpl::TransientActive(window_, &impl_->popup)) {
        WindowImpl::CloseTransient(window_);
    }
}

const std::optional<TimePicker::Time>& TimePicker::Value() const noexcept { return impl_->value; }

TimePicker& TimePicker::Value(std::optional<Time> value) {
    if (value) value = ClampTime(*value, impl_->minimum, impl_->maximum);
    impl_->value = value;
    impl_->display = value ? FormatTime(*value, impl_->display_mode) : std::wstring{};
    Invalidate();
    return *this;
}

TimePicker& TimePicker::Range(std::optional<Time> minimum, std::optional<Time> maximum) {
    if (minimum) minimum = ClampTime(*minimum, {}, {});
    if (maximum) maximum = ClampTime(*maximum, {}, {});
    if (minimum && maximum && *minimum > *maximum) std::swap(minimum, maximum);
    impl_->minimum = minimum;
    impl_->maximum = maximum;
    if (impl_->value) Value(impl_->value);
    return *this;
}

TimePicker& TimePicker::MinuteIncrement(int value) {
    value = std::clamp(value, 1, 30);
    impl_->increment = value;
    Invalidate();
    return *this;
}

int TimePicker::MinuteIncrement() const noexcept { return impl_->increment; }

TimePicker& TimePicker::DisplayMode(TimeDisplayMode value) {
    impl_->display_mode = value;
    impl_->use_24_hour = Resolve24Hour(value);
    if (impl_->value) impl_->display = FormatTime(*impl_->value, value);
    RelayoutParent();
    return *this;
}

TimeDisplayMode TimePicker::DisplayMode() const noexcept { return impl_->display_mode; }

TimePicker& TimePicker::Placeholder(std::wstring_view value) {
    impl_->placeholder.assign(value);
    Invalidate();
    return *this;
}

TimePicker& TimePicker::OnValueChanged(std::function<void(std::optional<Time>)> handler) {
    impl_->changed.Subscribe(std::move(handler));
    return *this;
}
Connection TimePicker::BindValueChanged(std::function<void(std::optional<Time>)> handler) {
    return impl_->changed.Connect(std::move(handler));
}

Size TimePicker::Measure(Size, const Theme& theme) {
    return {Resolve24Hour(impl_->display_mode) ? 136.0f : 148.0f, theme.input_height};
}

void TimePicker::Draw(Painter& painter, const Theme& theme) {
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
    painter.DrawIcon(icon::kClock, {absolute_.Right() - 40.0f, absolute_.y, 40.0f, absolute_.h},
                     16.0f, enabled_ ? theme.text_secondary : theme.text_disabled);
    if (focused_) PaintFocusRing(painter, theme, absolute_, theme.radius_control);
}

void TimePicker::OpenPopup() {
    if (!window_ || !enabled_) return;
    impl_->use_24_hour = Resolve24Hour(impl_->display_mode);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    Time initial = impl_->value.value_or(Time{now.wHour * 60 + now.wMinute});
    const int snapped = (MinuteCount(initial) / impl_->increment) * impl_->increment;
    impl_->popup.OpenAt(Time{snapped});
    WindowImpl::ShowTransient(window_, &impl_->popup, this, 280.0f, false);
}

bool TimePicker::OnKey(uint32_t vk) {
    if (vk == VK_RETURN || vk == VK_SPACE || vk == VK_DOWN) {
        OpenPopup();
        return true;
    }
    if (vk == VK_DELETE && impl_->value) {
        impl_->value.reset();
        impl_->display.clear();
        impl_->changed.Emit(impl_->value);
        Invalidate();
        return true;
    }
    return false;
}

void TimePicker::OnMouseDown(Point, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    OpenPopup();
}

void TimePicker::OnFocusChanged(bool focused) {
    focused_ = focused;
    Invalidate();
}

CursorShape TimePicker::CursorAt(Point) const { return CursorShape::Hand; }

TimePicker& TimePicker::BindValue(Property<std::optional<Time>>& p) {
    auto apply = [this, &p] {
        if (impl_->bind_loop) return;
        impl_->bind_loop = true;
        Value(p.Get());
        impl_->bind_loop = false;
    };
    apply();
    impl_->value_prop = ScopedConnection(p.OnChanged([apply](const std::optional<Time>&) { apply(); }));
    impl_->value_ctrl = ScopedConnection(impl_->changed.Connect([this, &p](std::optional<Time> v) {
        if (impl_->bind_loop) return;
        impl_->bind_loop = true;
        p = std::move(v);
        impl_->bind_loop = false;
    }));
    return *this;
}

} // namespace lumen

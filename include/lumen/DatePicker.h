// lumen/DatePicker.h — 跟随系统区域格式的公历日期选择器。
// Events: OnValueChanged / BindValueChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace lumen {

class DatePicker : public ControlOf<DatePicker> {
public:
    using Date = std::chrono::year_month_day;

    DatePicker();
    ~DatePicker() override;

    const std::optional<Date>& Value() const noexcept;
    DatePicker& Value(std::optional<Date> value);
    DatePicker& Range(std::optional<Date> minimum, std::optional<Date> maximum);
    DatePicker& Placeholder(std::wstring_view value);
    DatePicker& OnValueChanged(std::function<void(std::optional<Date>)> handler);
    Connection BindValueChanged(std::function<void(std::optional<Date>)> handler);
    DatePicker& BindValue(Property<std::optional<Date>>& p);

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnFocusChanged(bool focused) override;
    CursorShape CursorAt(Point local) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void OpenPopup();
};

} // namespace lumen

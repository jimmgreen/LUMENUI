// lumen/TimePicker.h — 支持系统、12 小时与 24 小时显示的当天时间选择器。
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

enum class TimeDisplayMode { System, TwelveHour, TwentyFourHour };

class TimePicker : public ControlOf<TimePicker> {
public:
    using Time = std::chrono::minutes;

    TimePicker();
    ~TimePicker() override;

    const std::optional<Time>& Value() const noexcept;
    TimePicker& Value(std::optional<Time> value);
    TimePicker& Range(std::optional<Time> minimum, std::optional<Time> maximum);
    TimePicker& MinuteIncrement(int value);
    int MinuteIncrement() const noexcept;
    TimePicker& DisplayMode(TimeDisplayMode value);
    TimeDisplayMode DisplayMode() const noexcept;
    TimePicker& Placeholder(std::wstring_view value);
    TimePicker& OnValueChanged(std::function<void(std::optional<Time>)> handler);
    Connection BindValueChanged(std::function<void(std::optional<Time>)> handler);
    TimePicker& BindValue(Property<std::optional<Time>>& p);

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

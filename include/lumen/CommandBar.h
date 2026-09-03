// lumen/CommandBar.h — 命令工具栏：放不下的项进 ⋯，溢出层复用 Menu。
// Events: OnInvoked / BindInvoked
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "InfoBadge.h"
#include "Signal.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

class Command;

enum class CommandBarItemType { Action, Toggle, Separator };

struct CommandBarItem {
    std::wstring id;
    std::wstring text;
    std::wstring glyph;
    CommandBarItemType type = CommandBarItemType::Action;
    bool enabled = true;
    bool checked = false;
    bool overflow_only = false;
    InfoBadgeData badge{};
    Command* command = nullptr;
};

class CommandBar : public ControlOf<CommandBar> {
public:
    CommandBar();
    ~CommandBar() override;

    CommandBar& Items(std::vector<CommandBarItem> items);
    const std::vector<CommandBarItem>& Items() const noexcept;
    CommandBar& ItemEnabled(std::wstring_view id, bool enabled);
    CommandBar& ItemChecked(std::wstring_view id, bool checked);
    CommandBar& ItemBadge(std::wstring_view id, InfoBadgeData badge);
    CommandBar& OnInvoked(std::function<void(std::wstring_view id, bool checked)> handler);
    Connection BindInvoked(std::function<void(std::wstring_view id, bool checked)> handler);
    CommandBar& Add(Command& command);

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    CursorShape CursorAt(Point local) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumen

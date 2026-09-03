#include "lumen/CommandBar.h"
#include "lumen/Command.h"
#include "lumen/Menu.h"
#include "lumen/Painter.h"
#include "lumen/Signal.h"
#include <windows.h>
#include <algorithm>
#include <utility>
#include <vector>

namespace lumen {
namespace {
constexpr float kHeight = 40.0f;
constexpr float kPad = 6.0f;
constexpr float kGap = 3.0f;
constexpr float kMoreWidth = 38.0f;
} // namespace

struct CommandBar::Impl {
    explicit Impl(CommandBar* control) : owner(control) {}

    float MeasureItemWidth(const CommandBarItem& item) {
        if (item.type == CommandBarItemType::Separator) return 9.0f;
        float width = item.text.empty() ? 36.0f : owner->MeasureText(item.text, TextRole::Body).w + 24.0f;
        if (!item.glyph.empty()) width += 22.0f;
        return std::max(width, 36.0f);
    }
    void InvalidateWidths() {
        widths_dirty = true;
        layout_dirty = true;
    }
    void RebuildWidths() {
        if (!widths_dirty) return;
        widths.clear();
        widths.reserve(items.size());
        for (const CommandBarItem& item : items) widths.push_back(MeasureItemWidth(item));
        widths_dirty = false;
    }
    void Reflow(float width) {
        RebuildWidths();
        if (!layout_dirty && layout_width == width) return;
        layout_width = width;
        layout_dirty = false;
        visible.clear();
        overflow.clear();
        float used = kPad;
        for (size_t i = 0; i < items.size(); ++i) {
            const CommandBarItem& item = items[i];
            if (item.overflow_only) {
                overflow.push_back(i);
                continue;
            }
            const float w = widths[i];
            const bool need_more = !overflow.empty() || i + 1 < items.size();
            const float reserve = need_more ? kMoreWidth + kGap : 0.0f;
            if (used + w + reserve + kPad <= width) {
                visible.push_back(i);
                used += w + kGap;
            } else {
                overflow.push_back(i);
            }
        }
        if (!overflow.empty()) {
            while (!visible.empty()) {
                float total = kPad + kMoreWidth + kPad;
                for (size_t index : visible) total += widths[index] + kGap;
                if (total <= width) break;
                overflow.insert(overflow.begin(), visible.back());
                visible.pop_back();
            }
        }
        if (focus >= static_cast<int>(visible.size()) + (!overflow.empty() ? 1 : 0)) focus = 0;
    }
    void SyncCommands() {
        for (auto& item : items) {
            if (item.command) {
                item.enabled = item.command->Enabled();
                item.text = item.command->Label();
                item.glyph = item.command->Glyph();
            }
        }
    }
    void Invoke(size_t index) {
        if (index >= items.size() || items[index].type == CommandBarItemType::Separator) {
            return;
        }
        CommandBarItem& item = items[index];
        if (item.command) item.enabled = item.command->Enabled();
        if (!item.enabled) return;
        if (item.type == CommandBarItemType::Toggle) {
            item.checked = !item.checked;
        }
        if (item.command) item.command->Execute();
        invoked.Emit(item.id, item.checked);
        owner->Invalidate();
    }
    int Hit(float x) const {
        float cursor = kPad;
        for (size_t slot = 0; slot < visible.size(); ++slot) {
            const float w = widths[visible[slot]];
            if (x >= cursor && x < cursor + w) return static_cast<int>(slot);
            cursor += w + kGap;
        }
        if (!overflow.empty() && x >= cursor && x < cursor + kMoreWidth) {
            return static_cast<int>(visible.size());
        }
        return -1;
    }
    float OverflowLocalX() const {
        float cursor = kPad;
        for (size_t slot = 0; slot < visible.size(); ++slot) {
            cursor += widths[visible[slot]] + kGap;
        }
        return cursor;
    }
    std::vector<size_t> OverflowRows() const {
        std::vector<size_t> rows;
        bool last_separator = true;
        for (size_t index : overflow) {
            const bool separator = items[index].type == CommandBarItemType::Separator;
            if (separator && last_separator) continue;
            rows.push_back(index);
            last_separator = separator;
        }
        while (!rows.empty() && items[rows.back()].type == CommandBarItemType::Separator) {
            rows.pop_back();
        }
        return rows;
    }
    void OpenOverflow() {
        Window* window = owner->WindowOf();
        if (!window || overflow.empty()) return;
        Menu menu;
        for (size_t index : OverflowRows()) {
            const CommandBarItem& item = items[index];
            if (item.type == CommandBarItemType::Separator) {
                menu.AddSeparator();
                continue;
            }
            MenuItem row;
            row.text = item.text;
            row.glyph = item.glyph;
            row.disabled = !item.enabled;
            row.checked = item.checked;
            row.action = [this, index] { Invoke(index); };
            menu.AddItem(std::move(row));
        }
        if (menu.Empty()) return;
        // 锚在 ⋯ 左缘、工具栏底边，与 MenuBar 点标题弹出同一套 Menu。
        menu.Popup(*window, {owner->AbsoluteBounds().x + OverflowLocalX(),
                             owner->AbsoluteBounds().Bottom()});
    }

    CommandBar* owner = nullptr;
    std::vector<CommandBarItem> items;
    std::vector<float> widths;
    std::vector<size_t> visible;
    std::vector<size_t> overflow;
    Signal<std::wstring_view, bool> invoked;
    std::vector<ScopedConnection> command_conns;
    int hover = -1;
    int focus = 0;
    float layout_width = -1.0f;
    bool layout_dirty = true;
    bool widths_dirty = true;
};

CommandBar::CommandBar() : impl_(std::make_unique<Impl>(this)) {}
CommandBar::~CommandBar() = default;

CommandBar& CommandBar::Items(std::vector<CommandBarItem> items) {
    impl_->items = std::move(items);
    impl_->InvalidateWidths();
    RelayoutParent();
    return *this;
}
const std::vector<CommandBarItem>& CommandBar::Items() const noexcept { return impl_->items; }
CommandBar& CommandBar::ItemEnabled(std::wstring_view id, bool enabled) {
    for (auto& i : impl_->items) {
        if (i.id == id) i.enabled = enabled;
    }
    Invalidate();
    return *this;
}
CommandBar& CommandBar::ItemChecked(std::wstring_view id, bool checked) {
    for (auto& i : impl_->items) {
        if (i.id == id) i.checked = checked;
    }
    Invalidate();
    return *this;
}
CommandBar& CommandBar::ItemBadge(std::wstring_view id, InfoBadgeData badge) {
    for (auto& i : impl_->items) {
        if (i.id == id) i.badge = badge;
    }
    Invalidate();
    return *this;
}
CommandBar& CommandBar::OnInvoked(std::function<void(std::wstring_view, bool)> handler) {
    impl_->invoked.Subscribe(std::move(handler));
    return *this;
}
Connection CommandBar::BindInvoked(std::function<void(std::wstring_view, bool)> handler) {
    return impl_->invoked.Connect(std::move(handler));
}

CommandBar& CommandBar::Add(Command& command) {
    CommandBarItem item;
    item.id = command.Label();
    item.text = command.Label();
    item.glyph = command.Glyph();
    item.enabled = command.Enabled();
    item.command = &command;
    impl_->items.push_back(std::move(item));
    Command* ptr = &command;
    impl_->command_conns.push_back(ScopedConnection(command.OnChanged([this, ptr] {
        for (auto& row : impl_->items) {
            if (row.command != ptr) continue;
            row.enabled = ptr->Enabled();
            row.text = ptr->Label();
            row.glyph = ptr->Glyph();
        }
        impl_->InvalidateWidths();
        Invalidate();
    })));
    impl_->InvalidateWidths();
    RelayoutParent();
    return *this;
}

Size CommandBar::Measure(Size available, const Theme&) {
    impl_->SyncCommands();
    impl_->RebuildWidths();
    float natural = kPad * 2.0f;
    bool overflow_only = false;
    for (size_t i = 0; i < impl_->items.size(); ++i) {
        if (impl_->items[i].overflow_only) {
            overflow_only = true;
            continue;
        }
        natural += impl_->widths[i] + kGap;
    }
    if (overflow_only) natural += kMoreWidth;
    const float width =
        (available.w >= 0.0f && available.w < 1.0e4f) ? available.w : std::max(natural, 120.0f);
    impl_->Reflow(width);
    return {width, kHeight};
}
void CommandBar::Arrange(const Rect& absolute) {
    Control::Arrange(absolute);
    impl_->Reflow(absolute.w);
}

void CommandBar::Draw(Painter& painter, const Theme& theme) {
    impl_->SyncCommands();
    painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);
    painter.StrokeRoundedRect(absolute_, theme.radius_control, theme.control_stroke);
    float x = absolute_.x + kPad;
    for (size_t slot = 0; slot < impl_->visible.size(); ++slot) {
        const CommandBarItem& item = impl_->items[impl_->visible[slot]];
        const float w = impl_->widths[impl_->visible[slot]];
        const Rect r{x, absolute_.y + 3.0f, w, absolute_.h - 6.0f};
        if (item.type == CommandBarItemType::Separator) {
            painter.FillRect({r.x + 4.0f, r.y + 7.0f, 1.0f, r.h - 14.0f}, theme.stroke_divider);
        } else {
            if (static_cast<int>(slot) == impl_->hover) {
                painter.FillRoundedRect(r, 7.0f, theme.fill_hover);
            }
            if (item.checked) painter.FillRoundedRect(r, 7.0f, theme.fill_selected);
            float tx = r.x + 9.0f;
            if (!item.glyph.empty()) {
                painter.DrawIcon(item.glyph, {tx, r.y, 22.0f, r.h}, 16.0f,
                                 item.enabled ? theme.text : theme.text_disabled);
                if (!item.badge.Empty()) {
                    PaintInfoBadge(painter, theme, {tx + 20.0f, r.y + 6.0f}, item.badge);
                }
                tx += 24.0f;
            } else if (!item.badge.Empty()) {
                PaintInfoBadge(painter, theme, {r.Right() - 4.0f, r.y + 6.0f}, item.badge);
            }
            painter.DrawText(item.text, {tx, r.y, r.Right() - tx - 8.0f, r.h}, TextRole::Body,
                             item.enabled ? theme.text : theme.text_disabled,
                             item.text.empty() ? Align::Center : Align::Leading);
            if (focused_ && static_cast<int>(slot) == impl_->focus) {
                PaintFocusRing(painter, theme, r, 7.0f);
            }
        }
        x += w + kGap;
    }
    if (!impl_->overflow.empty()) {
        const Rect more{x, absolute_.y + 3.0f, kMoreWidth, absolute_.h - 6.0f};
        if (impl_->hover == static_cast<int>(impl_->visible.size())) {
            painter.FillRoundedRect(more, 7.0f, theme.fill_hover);
        }
        painter.DrawText(L"⋯", more, TextRole::BodyStrong, theme.text, Align::Center);
        if (focused_ && impl_->focus == static_cast<int>(impl_->visible.size())) {
            PaintFocusRing(painter, theme, more, 7.0f);
        }
    }
}

bool CommandBar::OnKey(uint32_t vk) {
    const int count = static_cast<int>(impl_->visible.size()) + (!impl_->overflow.empty() ? 1 : 0);
    if (count <= 0) return false;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END) {
        if (vk == VK_HOME) impl_->focus = 0;
        else if (vk == VK_END) impl_->focus = count - 1;
        else impl_->focus = (impl_->focus + (vk == VK_LEFT ? count - 1 : 1)) % count;
        Invalidate();
        return true;
    }
    if (vk == VK_RETURN || vk == VK_SPACE) {
        if (impl_->focus < static_cast<int>(impl_->visible.size())) {
            impl_->Invoke(impl_->visible[static_cast<size_t>(impl_->focus)]);
        } else {
            impl_->OpenOverflow();
        }
        return true;
    }
    return false;
}

void CommandBar::OnMouseMove(Point local, uint32_t) {
    const int hit = impl_->Hit(local.x);
    if (hit != impl_->hover) {
        impl_->hover = hit;
        Invalidate();
    }
}

void CommandBar::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    const int hit = impl_->Hit(local.x);
    if (hit < 0) return;
    impl_->focus = hit;
    if (hit < static_cast<int>(impl_->visible.size())) {
        impl_->Invoke(impl_->visible[static_cast<size_t>(hit)]);
    } else {
        impl_->OpenOverflow();
    }
}

void CommandBar::OnMouseLeave() {
    Control::OnMouseLeave();
    impl_->hover = -1;
    Invalidate();
}

void CommandBar::OnFocusChanged(bool focused) {
    focused_ = focused;
    Invalidate();
}

CursorShape CommandBar::CursorAt(Point) const { return CursorShape::Hand; }

} // namespace lumen

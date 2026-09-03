#include "lumen/MenuBar.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace lumen {
namespace {
constexpr float kPadX = 10.0f;    // 标题左右内边距
constexpr float kGapX = 2.0f;     // 标题间距
} // namespace

MenuBar& MenuBar::AddMenu(std::wstring_view title, Menu menu) {
    titles_.emplace_back(title);
    menus_.push_back(std::move(menu));
    RelayoutParent();
    return *this;
}

Size MenuBar::Measure(Size, const Theme& theme) {
    title_x_.clear();
    title_w_.clear();
    float x = kPadX;
    for (const std::wstring& title : titles_) {
        const Size text = MeasureText(MenuLabel(title), TextRole::Body);
        text_h_ = text.h;
        const float w = text.w + kPadX * 2.0f;
        title_x_.push_back(x);
        title_w_.push_back(w);
        x += w + kGapX;
    }
    return {std::max(x, 60.0f), theme.input_height};
}

int MenuBar::TitleAt(float x) const {
    for (size_t i = 0; i < titles_.size(); ++i) {
        if (x >= title_x_[i] && x < title_x_[i] + title_w_[i]) return static_cast<int>(i);
    }
    return -1;
}

void MenuBar::OpenMenu(int index) {
    Window* window = WindowOf();
    if (!window || index < 0 || index >= static_cast<int>(menus_.size())) return;
    // 菜单是模态弹层：弹在标题正下方，关闭后本函数才返回。
    Menu popup = menus_[static_cast<size_t>(index)];
    Point client{absolute_.x + title_x_[static_cast<size_t>(index)], absolute_.Bottom()};
    popup.Popup(*window, client);
}

bool MenuBar::ActivateMnemonic(uint32_t vk) {
    wchar_t key = 0;
    if (vk >= 'A' && vk <= 'Z') key = static_cast<wchar_t>(vk);
    else if (vk >= 'a' && vk <= 'z') key = static_cast<wchar_t>(towupper(static_cast<wint_t>(vk)));
    else if (vk >= '0' && vk <= '9') key = static_cast<wchar_t>(vk);
    if (!key) return false;
    for (size_t i = 0; i < titles_.size(); ++i) {
        if (MenuAccessKey(titles_[i]) == key) {
            OpenMenu(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

void MenuBar::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    const int index = TitleAt(local.x);
    if (index >= 0) OpenMenu(index);
}

void MenuBar::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    const int index = TitleAt(local.x);
    if (index != hover_) {
        hover_ = index;
        Invalidate();
    }
}

void MenuBar::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_ != -1) {
        hover_ = -1;
        Invalidate();
    }
}

CursorShape MenuBar::CursorAt(Point) const { return CursorShape::Hand; }

void MenuBar::Draw(Painter& painter, const Theme& theme) {
    if (titles_.empty() || absolute_.IsEmpty()) return;
    for (size_t i = 0; i < titles_.size(); ++i) {
        const Rect slot{absolute_.x + title_x_[i], absolute_.y, title_w_[i], absolute_.h};
        if (static_cast<int>(i) == hover_) {
            painter.FillRoundedRect(slot.Inset(2.0f, 4.0f), theme.radius_control * 0.6f,
                                    theme.fill_hover);
        }
        // DrawText 顶对齐且默认左对齐：行盒在 bar 内垂直居中、文字在 slot 内水平居中，
        // 与悬停胶囊同心。
        std::wstring display;
        int index = -1;
        wchar_t parsed = 0;
        MenuParseAccess(titles_[i], display, index, parsed);
        (void)parsed;
        const Rect text_slot{slot.x, absolute_.y + (absolute_.h - text_h_) * 0.5f, slot.w, text_h_};
        painter.DrawText(display, text_slot, TextRole::Body, theme.text, Align::Center);
        DrawMnemonicUnderline(painter, display, index, text_slot, TextRole::Body, theme.text,
                              Align::Center, WindowImpl::LumaOf(WindowOf()));
    }
}

} // namespace lumen

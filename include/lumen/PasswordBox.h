// lumen/PasswordBox.h — 口令框：显示掩码圆点，禁止复制明文；可选明文揭示开关。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "TextBox.h"

namespace lumen {

class PasswordBox : public TextBox {
public:
    PasswordBox();
    explicit PasswordBox(std::wstring_view text);

    // 尾部小眼睛开关：点击在掩码/明文间切换（文本与光标位置保留）。
    PasswordBox& Revealable(bool value);
    bool Revealable() const noexcept { return revealable_; }
    bool Revealed() const noexcept { return revealed_; }

protected:
    friend class WindowImpl;
    bool AutomationIsPassword() const noexcept override { return password_ && !revealed_; }
    void Draw(Painter& painter, const Theme& theme) override;
    CursorShape CursorAt(Point local) const override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    float PadRight() const override;

private:
    bool InRevealZone(Point local) const;

    bool revealable_ = false;
    bool revealed_ = false;
};

} // namespace lumen

// lumen/BusyOverlay.h — 窗口忙碌遮罩内容（通常经 Window::ShowBusy 使用）。
// Events: OnCancel / BindCancel
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class Label;
class ProgressRing;
class Button;

class BusyOverlay : public StackPanel {
public:
    BusyOverlay();
    ~BusyOverlay() override;

    BusyOverlay& Message(std::wstring_view value);
    const std::wstring& Message() const noexcept;
    BusyOverlay& OnCancel(std::function<void()> handler);
    Connection BindCancel(std::function<void()> handler);

protected:
    friend class WindowImpl;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnKey(uint32_t vk) override;
    bool Focusable() const noexcept override { return true; }

    void RequestClose();
    void ArmAnimation();

    ProgressRing* ring_ = nullptr;
    Label* label_ = nullptr;
    Button* cancel_btn_ = nullptr;
    Signal<> cancel_sig_;
};

} // namespace lumen

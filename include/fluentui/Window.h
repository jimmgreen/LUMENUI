// fluentui/Window.h — 顶层窗口：渲染、输入路由、焦点、动画时钟、主题跟随。
#pragma once
#include "Core.h"
#include "Theme.h"
#include "Panel.h"
#include <functional>
#include <memory>
#include <string_view>

namespace fui {

class Dialog;

class Window {
public:
    // 尺寸为客户区 DIP。主题默认跟随系统亮暗与强调色。
    Window(std::wstring_view title, Size client_size);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 根容器（纵向堆叠）。需要自由布局时在里面放一个普通 Panel。
    StackPanel& Root();

    void Show();
    void Close();
    bool Closed() const;

    void Title(std::wstring_view text);
    void Resize(Size client_size);
    void MinSize(Size min_size);

    ThemeMode Theme() const;
    void SetTheme(ThemeMode mode);

    // 返回 false 可取消关闭。
    void OnClosing(std::function<bool()> callback);

    // 模态对话框（窗口内覆盖层）。同窗口一次只允许一个。
    void ShowDialog(Dialog& dialog);
    void CloseDialog();
    bool DialogActive() const;

    void Invalidate();

    void* NativeHandle() const;   // HWND

private:
    friend class Control;
    friend class Dialog;
    friend class Menu;
    friend class WindowImpl;
    class WindowImpl* Impl() const noexcept;

    std::unique_ptr<class WindowImpl> impl_;
};

} // namespace fui

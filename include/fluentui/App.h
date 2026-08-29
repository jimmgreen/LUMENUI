// fluentui/App.h — 进程级初始化与消息循环。
#pragma once
#include <cstdint>

namespace fui {

class App {
public:
    // 开启 Per-Monitor V2 DPI 感知并注册窗口类；进程内构造一次即可。
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();                              // 标准消息循环
    static void Quit(int exit_code = 0);    // PostQuitMessage
};

} // namespace fui

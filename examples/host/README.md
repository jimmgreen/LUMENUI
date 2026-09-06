# 外部宿主与 DLL 接入模板

`host_plugin.cpp` 编译成 DLL，提供 owned-window 和 embedded-child 两种接入。`host_driver.cpp` 代表宿主，拥有主窗口、DPI 策略和外部消息泵；同一 UI 线程调用全部导出。运行 `build/lumen_host_test.exe` 对两种模式各执行 12 轮 DLL 加载、显示、隐藏复用、尺寸同步、销毁、Shutdown 和卸载。

真实插件可复用这些导出函数的顺序，将宿主 HWND 传入 HostOpen。嵌入时父窗尺寸变化调用 HostResize，传物理像素；DPI 上下文通过 WindowSpec.matchDpiHwnd 匹配父窗。HostHide 保留输入草稿；HostDestroy 销毁窗口包装对象，不能用可被否决的 Close 代替。宿主继续泵消息并重试 HostCanUnload；返回 false 时保留 DLL，不能直接 FreeLibrary。通过后才允许卸载。

不要在 DllMain 中建窗、初始化 COM 或等待任务。普通后台工作用独立 UiDispatcher 返回 UI；CAD 文档锁、CAD 操作调度、模态禁用、焦点恢复到宿主及全局快捷键策略仍由宿主决定。关闭页面不代表后台线程已退出。

此模板验证标准 Win32 宿主路径；AutoCAD 停靠、真实 IME 候选、多屏不同 DPI 和真实读屏交互仍需在目标宿主验收。

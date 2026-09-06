# 更新日志

面向使用者的显著变更，升级注意事项优先。版本遵循语义化：0.x 阶段次版本号代表新能力，修订号代表修复。

## v0.2.0 — 2026-09-06

主题：**原生宿主嵌入**（AutoCAD .arx / MFC / Win32 外壳）与**宿主模式呈现强化**。本次起 Gallery 与 SDK 由 GitHub Actions 在 `windows-latest` 上编译发布（打 `v*` tag 自动出包）。

### 新增：原生嵌入 API

- `WindowSpec.parent`：非空时直接创建 `WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP` 子窗，DPI 优先匹配父窗（无需再靠 `matchDpiHwnd` 对齐感知上下文）；`NativeHandle()` 始终返回该子窗。嵌入态跳过系统边框修正与 DWM 圆角/暗色 chrome——尺寸、阴影与关闭归外壳负责。
- `WindowSpec.frameTarget`：指定原生外壳后，Client 标题栏命中（`WM_NCHITTEST`）与边框缩放判定路由到外壳；标题栏按钮按外壳最大化状态绘制；`AdjustFrameRect` 用外壳实际外框减客户区的余量（兼容自绘无边框外壳）；`MinSize` 经外壳 `WM_GETMINMAXINFO` 转发给子窗执行。
- `App::HasActiveCallbacks()`（`noexcept`）：原生/UIA/OLE 回调或阻塞菜单、弹层、拖动会话在途的只读快照。宿主在 UI 线程开始破坏性清理前轮询；`false` 不是跨线程卸载锁——仍须阻止新任务、关闭窗口，再用 `App::CanShutdown()` 最终验收。
- 嵌入外壳契约（详见 `skills/lumen/references/use.md` 宿主章节）：
  - 外壳 `WM_SIZE` 将子窗填满客户区；`WM_NCHITTEST` 可转发子窗复用 Client 标题栏；`WM_DPICHANGED` 按建议位置更新外壳后可转发，子窗仅更新 DPI 与布局。
  - MFC 外壳 `PreTranslateMessage` 对子窗及后代返回 `FALSE`，让 LUMEN 处理 Tab/Enter/Esc 与 IME；子窗提供 `WM_GETDLGCODE` 全键盘标志。
  - 子窗 `Close` 先走 `OnClosing`（可否决），通过后异步发送外壳 `WM_CLOSE`；外壳取消时可先转发子窗 `WM_CLOSE` 执行否决逻辑；外壳自己的关闭协议不再回传子窗。窗口析构必须由创建线程完成。
  - Resize 对外壳使用异步 `SetWindowPos`；外壳与子窗避免相互同步等待。

### 变更：宿主模式呈现与弹层归属

- 宿主模式（`App::HostMode(true)`）帧呈现改为 `Present(0, DXGI_PRESENT_DO_NOT_WAIT)`；`DXGI_ERROR_WAS_STILL_DRAWING` 保留 retain 位图与待呈现状态，下次提交以全量拷贝覆盖先前脏区，不再当作设备丢失或立即重试。独立 App 仍随 `Present(1,0)` 垂直同步，行为不变。
- 主窗、菜单与弹层合并为一次性计时唤醒，默认约 60Hz；窗口隐藏或最小化时停止计帧，渲染器析构与关停时摘除计时器。宿主仍无需为 lumen 增加渲染线程。
- 菜单与独立弹层的原生 owner 改挂根外壳（`GA_ROOT`），保持 `WS_EX_NOACTIVATE` 不抢激活；菜单按 owner 的 DPI 上下文创建，混合 DPI 多显示器下弹层缩放正确。
- `WindowSpec.matchDpiHwnd` 语义收窄为「调用方自行 `SetParent` 的兼容路径」；原生嵌入请改用 `parent`（可选加 `frameTarget`）。

### 变更：关停门禁更严格（升级注意）

- `App::CanShutdown()` / `App::Shutdown()` 现在同时等待：原生回调（窗口过程、`MenuWindow::Show`/`PopupWindow::Show`、文本拖放等阻塞会话）清零、OLE 代理对象（拖放目标/数据源等宿主仍可能持有的接口）析构、UIA 无引用、`RunAsync` 任务全部退出。v0.1.0 能通过的卸载路径如果仍挂着拖放或菜单会话，升级后会拒绝关停并抛出——先用 `HasActiveCallbacks()` 轮询、让会话退出，再验收。
- UIA 提供程序的每个 COM 入口（`QueryInterface`/引用计数/各 pattern 调用）都进入回调计数；宿主在 UIA 回调中触发卸载会被门禁拦下，不再静默半卸载。

### 修复

- 窗口类注册数组按 `kClassNames` 实际数量分配：此前 `lumen_popup` 类从不注销，模块重载会遗留陈旧类导致弹层建窗失败。
- 菜单创建的 DPI 上下文跟随 owner（此前用默认线程上下文）。
- 延迟帧重试时合并此前所有脏区，避免 retain 位图部分像素滞留旧内容。

### 构建与接入

- 新增 `LUMEN_USE_PREBUILT_LUMATEXT` 选项：跳过同级 `../lumatext` 源码探测，仅用 `find_package(LumaText)` 接入；`find_package` 分支同时接受安装导出的 `LumaText::Shared`（此前要求 `LumaText::D2D`，lumatext 安装导出并不提供），预编译包路径下运行时拷贝与 SDK 安装照常生效。
- 版本号升至 0.2.0；README 的 FetchContent 示例 `GIT_TAG` 同步为 `v0.2.0`；宿主嵌入章节补充 `parent`/`frameTarget` 与卸载门禁说明。
- SDK 升级：0.2.x 与 0.1.x 同主版本兼容（`SameMajorVersion`），SDK zip 解压替换后 `find_package(lumen CONFIG)` 原样可用。

### 发布工程

- 新增 `.github/workflows/release.yml`：推 `v*` tag 时在 GitHub `windows-latest` 上编译，产出 `lumen-gallery-windows-x64.zip`（exe + `lumatext.dll` + 许可证 + VC 运行库）与 `lumen-sdk-windows-x64.zip`（`cmake --install` 产物），发布说明由 `tools/extract_changelog.py` 从本文件按版本截取。
- CI 不编译 lumatext：从 `lumatext-deps` 预发布下载预编译产物（URL 与 SHA256 在工作流中钉定），经 `find_package(LumaText)` 链接。

### 验证

- `build.bat` 同规格（Ninja/Release）全量构建通过，`lumen_api_test` 命名检查与 README 代码块编译通过。
- `lumen_visual_test` 全部 PASS，含本次新增的原生回调计时泵断言与嵌入子窗生命周期断言（并入 `TestHostCycle` 12 轮建窗、泵消息、Close、Shutdown：无 WARP、无 `WM_QUIT`、加载引用释放、owner 正确、弹层类注销）。
- `lumen_anim_test` 缓动/补间/弹簧断言 ALL PASS；`lumen_perf_test` 1280×800 典型界面（8 按钮 + 100,000 行虚拟列表 + 聚光卡 + Area/Heatmap）本次实测平均 1.606 ms/帧、最差 2.393 ms/帧（预算 < 8 ms），Draw 零 C++ 堆分配。
- Gallery 实机启动冒烟通过（渲染、布局正常）。真实鼠标手感、IME 组合输入、AutoCAD/MFC 实机嵌入为待人工确认项。

## v0.1.0 — 2026-09-03

首个发布。

- Gallery 免编译体验包：解压运行 `lumen_gallery.exe`，自带 `lumatext.dll` 与 VC 运行库。
- Windows SDK zip：`lumen.lib` + 公共头 + `lumatext`，`find_package(lumen CONFIG)` 即可接入。

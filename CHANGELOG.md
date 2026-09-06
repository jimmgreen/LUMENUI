# 更新日志

## v0.2.0 — 2026-09-06

原生宿主嵌入能力（.arx / MFC / Win32 外壳）与宿主模式呈现强化。

### 新增

- `WindowSpec.parent`：非空时直接创建 `WS_CHILD` 子窗，DPI 优先匹配父窗；`NativeHandle()` 始终返回该子窗。
- `WindowSpec.frameTarget`：指定原生外壳接管 Client 标题栏命中与 Resize 路由；外壳把 `WM_GETMINMAXINFO` 转发给子窗以应用 `MinSize`，外壳负责布局、关闭与阴影。
- `App::HasActiveCallbacks()`：`noexcept` 只读快照，报告原生/UIA/OLE 回调或阻塞菜单、弹层、拖动会话是否在途，供宿主在破坏性清理前轮询；最终验收仍以 `App::CanShutdown()` 为准。
- 菜单与独立弹层的原生 owner 改挂根外壳（`GA_ROOT`），保持不激活显示；嵌入外壳时输入与 IME 仍以 LUMEN 子窗为源。

### 升级注意

- `App::CanShutdown()` / `App::Shutdown()` 门禁更严格：原生回调（窗口过程、菜单/弹层/拖放会话）或 OLE 代理对象在途时会拒绝关停并抛出异常。v0.1.0 能通过的卸载路径如果仍挂着拖放或菜单会话，升级后请先让会话退出，再验收关停。
- 宿主模式（`App::HostMode(true)`）呈现退让：改用 `Present(0, DXGI_PRESENT_DO_NOT_WAIT)`；主窗、菜单、弹层合并为一次约 60Hz 的计时唤醒，窗口隐藏或最小化时停止；`DXGI_ERROR_WAS_STILL_DRAWING` 保留脏区待下次全量提交，不视为设备丢失。宿主不需要为 lumen 增加渲染线程。
- `WindowSpec.matchDpiHwnd` 语义收窄为「调用方自行 `SetParent` 的兼容路径」；原生嵌入请改用 `parent`（可选加 `frameTarget`）。
- 外壳契约：外壳 `WM_SIZE` 把子窗填满客户区，`WM_NCHITTEST` 可转发子窗复用 Client 标题栏；MFC 外壳 `PreTranslateMessage` 对子窗及后代返回 FALSE，让 LUMEN 处理 Tab/Enter/Esc/IME。详见 `skills/lumen/references/use.md` 宿主章节。

### SDK 与接入

- 0.2.x 与 0.1.x 同主版本兼容（`SameMajorVersion`）；SDK zip 解压替换后 `find_package(lumen CONFIG)` 原样可用。
- FetchContent 接入把 `GIT_TAG` 提到 `v0.2.0`。
- 预编译 LumaText 包可用 `find_package(LumaText)` 以 `LumaText::Shared` 接入（此前预编译路径要求 `LumaText::D2D`，lumatext 安装导出并不提供）；`LUMEN_REQUIRE_LUMATEXT=ON` 时找不到即 configure 失败。

### 验证

- Ninja/Release 全量构建通过；`lumen_visual_test`（含新增原生回调与嵌入子窗断言、`TestHostCycle` 12 轮建窗/泵消息/关闭/关停）、`lumen_anim_test`、`lumen_api_test` 全部通过；`lumen_perf_test` 1280×800 典型界面（8 按钮 + 10 万行虚拟列表 + 聚光卡 + 图）本次实测平均 1.606 ms/帧、最差 2.393 ms/帧（预算 < 8 ms）。

## v0.1.0 — 2026-09-03

首个发布。

- Gallery 免编译体验包：解压运行 `lumen_gallery.exe`，自带 `lumatext.dll` 与 VC 运行库。
- Windows SDK zip：`lumen.lib` + 公共头 + `lumatext`，`find_package(lumen CONFIG)` 即可接入。

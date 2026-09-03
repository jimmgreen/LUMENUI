// window_impl.cpp — 窗口生命周期、Win32 消息、布局与绘制。
// 输入 / 浮层 / 客户区边框 / IME / 托盘 / 定时器分别在 input_router、overlay_host、
// frame_chrome、ime_bridge、tray_host、timer_host。
#include "window_impl.h"
#include "app_host.h"
#include "hotkey.h"
#include "lumatext_bridge.h"
#include "lumen/App.h"
#include "lumen/BusyOverlay.h"
#include "lumen/Command.h"
#include "lumen/Control.h"
#include "lumen/Dialog.h"
#include "lumen/Drawer.h"
#include "lumen/Flyout.h"
#include "lumen/MenuBar.h"
#include "lumen/TeachingTip.h"
#include "lumen/ToolTip.h"
#include "lumen/Icons.h"
#include "lumen/ScrollViewer.h"
#include "lumen/TextBox.h"
#include "lumen/TitleBar.h"
#include "lumen/Animate.h"
#include "log.h"
#include <windowsx.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <imm.h>
#include <ole2.h>
#include <typeinfo>
#include <ostream>
#include <thread>
#include <shlobj.h>
#include <shellapi.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <deque>
#include <mutex>
#include <string_view>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {
// 存活窗口数：最后一个窗口销毁时投 WM_QUIT，App::Run 才能返回（否则进程僵留）。
int g_live_windows = 0;
constexpr UINT kWmPost = WM_APP + 0x20;
} // namespace
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFEu
#endif
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif

namespace lumen {
namespace {

float QpcMs(LARGE_INTEGER start, LARGE_INTEGER freq) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return 1000.0f * static_cast<float>(now.QuadPart - start.QuadPart) /
           static_cast<float>(freq.QuadPart);
}

} // namespace

struct WindowImpl::OleDropTarget : IDropTarget {
    explicit OleDropTarget(WindowImpl* owner) : owner_(owner) {}
    ~OleDropTarget() { Reset(); }
    void Detach() noexcept {
        Reset();
        owner_ = nullptr;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    STDMETHODIMP DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        if (!effect) return E_INVALIDARG;
        const DWORD allowed = *effect;
        *effect = DROPEFFECT_NONE;
        Reset();
        if (!owner_ || !data) return S_OK;
        data->AddRef();
        data_ = data;
        has_hdrop_ = QueryHdrop(data);
        has_text_ = QueryText(data);
        Update(pt, keys, effect, allowed);
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        if (!effect) return E_INVALIDARG;
        const DWORD allowed = *effect;
        *effect = DROPEFFECT_NONE;
        if (!owner_) return S_OK;
        Update(pt, keys, effect, allowed);
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        Describe(DROPIMAGE_INVALID);
        Reset();
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        if (!effect) return E_INVALIDARG;
        const DWORD allowed = *effect;
        *effect = DROPEFFECT_NONE;
        IDataObject* src = data ? data : data_;
        if (!owner_ || !src) {
            Reset();
            return S_OK;
        }
        const Point dip = owner_->DipFromScreen(pt.x, pt.y);
        if (has_text_ || QueryText(src)) {
            if (Control* zone = owner_->TextDropAt(dip)) {
                const std::wstring text = ExtractText(src);
                if (!text.empty()) {
                    *effect = (allowed & DROPEFFECT_COPY) != 0 ? DROPEFFECT_COPY
                                                               : ChooseEffect(keys, allowed);
                    Describe(ImageFor(*effect));
                    Reset();
                    zone->OnTextDrop(text, dip);
                    owner_->Invalidate();
                    return S_OK;
                }
            }
        }
        Control* zone = owner_->FileDropAt(dip);
        auto files = ExtractHdrop(src);
        std::vector<std::wstring> accepted;
        if (zone) accepted = zone->FilterFileDrop(std::move(files));
        if (zone && !accepted.empty()) *effect = ChooseEffect(keys, allowed);
        Describe(ImageFor(*effect));
        Reset();
        if (!zone || accepted.empty()) return S_OK;
        zone->OnFileDrop(std::move(accepted));
        owner_->Invalidate();
        return S_OK;
    }

private:
    static FORMATETC HdropFormat() noexcept {
        return {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    }
    static FORMATETC TextFormat() noexcept {
        return {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    }
    static bool QueryHdrop(IDataObject* data) {
        FORMATETC fmt = HdropFormat();
        return data && data->QueryGetData(&fmt) == S_OK;
    }
    static bool QueryText(IDataObject* data) {
        FORMATETC fmt = TextFormat();
        return data && data->QueryGetData(&fmt) == S_OK;
    }
    static std::wstring ExtractText(IDataObject* data) {
        if (!data) return {};
        FORMATETC fmt = TextFormat();
        STGMEDIUM medium{};
        if (data->GetData(&fmt, &medium) != S_OK) return {};
        std::wstring out;
        if (medium.hGlobal) {
            if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(medium.hGlobal))) {
                out = p;
                GlobalUnlock(medium.hGlobal);
            }
        }
        ReleaseStgMedium(&medium);
        return out;
    }
    static CLIPFORMAT DescriptionFormat() {
        static const CLIPFORMAT fmt =
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_DROPDESCRIPTION));
        return fmt;
    }
    static DROPIMAGETYPE ImageFor(DWORD effect) noexcept {
        if (effect & DROPEFFECT_COPY) return DROPIMAGE_COPY;
        if (effect & DROPEFFECT_MOVE) return DROPIMAGE_MOVE;
        if (effect & DROPEFFECT_LINK) return DROPIMAGE_LINK;
        return DROPIMAGE_NONE;
    }
    // 资源管理器读 CFSTR_DROPDESCRIPTION。不走 IDropTargetHelper：目标 HWND 是
    // WS_EX_NOREDIRECTIONBITMAP，Helper 会堵住 DragEnter，GiveFeedback 出不来。
    void Describe(DROPIMAGETYPE type) {
        if (!data_ || type == last_image_) return;
        last_image_ = type;
        HGLOBAL mem = GlobalAlloc(GHND, sizeof(DROPDESCRIPTION));
        if (!mem) return;
        if (DROPDESCRIPTION* desc = static_cast<DROPDESCRIPTION*>(GlobalLock(mem))) {
            desc->type = type;
            GlobalUnlock(mem);
        }
        FORMATETC fmt{DescriptionFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = mem;
        if (FAILED(data_->SetData(&fmt, &medium, TRUE))) ReleaseStgMedium(&medium);
    }
    static DWORD ChooseEffect(DWORD keys, DWORD allowed) noexcept {
        DWORD want = DROPEFFECT_MOVE;
        if ((keys & (MK_CONTROL | MK_SHIFT)) == (MK_CONTROL | MK_SHIFT)) want = DROPEFFECT_LINK;
        else if (keys & MK_CONTROL) want = DROPEFFECT_COPY;
        if (want == DROPEFFECT_MOVE && (allowed & DROPEFFECT_COPY)) want = DROPEFFECT_COPY;
        if (want & allowed) return want;
        if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
        if (allowed & DROPEFFECT_MOVE) return DROPEFFECT_MOVE;
        if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
        return DROPEFFECT_NONE;
    }
    static std::vector<std::wstring> ExtractHdrop(IDataObject* data) {
        std::vector<std::wstring> paths;
        if (!data) return paths;
        FORMATETC fmt = HdropFormat();
        STGMEDIUM medium{};
        if (FAILED(data->GetData(&fmt, &medium))) return paths;
        if (medium.hGlobal) {
            if (HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
                const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i) {
                    const UINT chars = DragQueryFileW(drop, i, nullptr, 0);
                    if (chars == 0) continue;
                    std::wstring path(static_cast<size_t>(chars) + 1, L'\0');
                    const UINT wrote = DragQueryFileW(drop, i, path.data(), chars + 1);
                    if (wrote > 0) {
                        path.resize(wrote);
                        paths.push_back(std::move(path));
                    }
                }
                GlobalUnlock(medium.hGlobal);
            }
        }
        ReleaseStgMedium(&medium);
        return paths;
    }
    void Reset() {
        last_image_ = DROPIMAGE_INVALID;
        if (data_) {
            data_->Release();
            data_ = nullptr;
        }
        has_hdrop_ = false;
        has_text_ = false;
        if (owner_) owner_->SetDropArmed(nullptr);
    }
    void Update(POINTL pt, DWORD keys, DWORD* effect, DWORD allowed) {
        const Point dip = owner_->DipFromScreen(pt.x, pt.y);
        if (has_hdrop_) {
            *effect = ChooseEffect(keys, allowed);
            owner_->SetDropArmed(owner_->FileDropAt(dip));
            Describe(ImageFor(*effect));
            return;
        }
        owner_->SetDropArmed(nullptr);
        if (has_text_ && owner_->TextDropAt(dip)) {
            *effect = (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : ChooseEffect(keys, allowed);
            Describe(ImageFor(*effect));
            return;
        }
        Describe(DROPIMAGE_NONE);
    }

    WindowImpl* owner_ = nullptr;
    IDataObject* data_ = nullptr;
    DROPIMAGETYPE last_image_ = DROPIMAGE_INVALID;
    bool has_hdrop_ = false;
    bool has_text_ = false;
    LONG refs_ = 1;
};

namespace {

struct TextDropSource : IDropSource {
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if ((keys & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
    LONG refs_ = 1;
};

struct TextDataObject : IDataObject {
    explicit TextDataObject(std::wstring_view text) : text_(text) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    STDMETHODIMP GetData(FORMATETC* fmt, STGMEDIUM* medium) override {
        if (!fmt || !medium) return E_POINTER;
        if (fmt->cfFormat != CF_UNICODETEXT || (fmt->tymed & TYMED_HGLOBAL) == 0) {
            return DV_E_FORMATETC;
        }
        const SIZE_T bytes = (text_.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GHND, bytes);
        if (!mem) return E_OUTOFMEMORY;
        if (void* p = GlobalLock(mem)) {
            std::memcpy(p, text_.c_str(), bytes);
            GlobalUnlock(mem);
        }
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = mem;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryGetData(FORMATETC* fmt) override {
        if (!fmt) return E_POINTER;
        if (fmt->cfFormat == CF_UNICODETEXT && (fmt->tymed & TYMED_HGLOBAL) != 0) return S_OK;
        return DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (!out) return E_POINTER;
        *out = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        return DATA_S_SAMEFORMATETC;
    }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** enum_fmt) override {
        if (!enum_fmt) return E_POINTER;
        *enum_fmt = nullptr;
        if (dir != DATADIR_GET) return E_NOTIMPL;
        FORMATETC fmt{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        return SHCreateStdEnumFmtEtc(1, &fmt, enum_fmt);
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
    std::wstring text_;
    LONG refs_ = 1;
};

} // namespace

DWORD WindowImpl::DragUnicodeText(std::wstring_view text) {
    if (text.empty()) return DROPEFFECT_NONE;
    auto* data = new TextDataObject(text);
    auto* source = new TextDropSource();
    DWORD effect = DROPEFFECT_NONE;
    DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    data->Release();
    source->Release();
    return effect;
}

Window::Window(std::wstring_view title, Size client_size, Frame frame) {
    App::Ensure();
#ifndef NDEBUG
    if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                      DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        Control::DebugTrap(L"LUMEN_CHECK: process DPI awareness is not Per-Monitor V2");
    }
#endif
    // impl_ stays null until this assignment returns. WindowImpl's ctor CreateWindow
    // can re-enter Paint; controls must not call window_->Impl() on a half-built unique_ptr.
    impl_ = std::make_unique<WindowImpl>(this, title, client_size, frame);
}

Window::Window(std::wstring_view title) : Window(WindowSpec{.title = std::wstring(title)}) {}

Window::Window(WindowSpec spec) {
    App::Ensure();
#ifndef NDEBUG
    if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                      DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        Control::DebugTrap(L"LUMEN_CHECK: process DPI awareness is not Per-Monitor V2");
    }
#endif
    Size size = spec.size;
    if (size.w <= 0.0f) size.w = 960.0f;
    if (size.h <= 0.0f) size.h = 640.0f;
    impl_ = std::make_unique<WindowImpl>(this, spec.title, size, spec.frame);
    Backdrop(spec.backdrop);
    MinSize({size.w * 0.6f, size.h * 0.6f});
    impl_->LoadFirstExeIcon();
}

Window::~Window() = default;

WindowImpl* Window::Impl() const noexcept { return impl_.get(); }
StackPanel& Window::Root() { return impl_->Root(); }
TitleBar* Window::TitleBar() { return impl_->TitleBarPtr(); }
void Window::Show() {
    if (impl_->Root().ChildCount() == 0) Log(LogLevel::Warn, L"empty root");
    impl_->Show();
}
void Window::Close() { impl_->Close(); }
bool Window::Closed() const { return impl_->Closed(); }
void Window::Title(std::wstring_view text) { impl_->Title(text); }
void Window::Resize(Size client_size) { impl_->Resize(client_size); }
void Window::MinSize(Size min_size) { impl_->MinSize(min_size); }
void Window::GlowIntensity(float intensity) { impl_->GlowIntensity(intensity); }
float Window::GlowIntensity() const { return impl_->glow_intensity_; }
lumen::Backdrop Window::Backdrop() const { return impl_->backdrop_; }
void Window::Backdrop(lumen::Backdrop backdrop) { impl_->SetBackdrop(backdrop); }
const Theme& Window::VisualTheme() const { return impl_->theme_; }
void Window::OnClosing(std::function<bool()> callback) { impl_->closing_ = std::move(callback); }
void Window::ShowDialog(Dialog& dialog) { impl_->ShowDialog(dialog); }

void Window::ShowDialog(std::unique_ptr<Dialog> dialog) { impl_->ShowDialog(std::move(dialog)); }

void Window::ShowDialog(DialogSpec spec) { impl_->ShowDialog(std::move(spec)); }

void Window::Confirm(std::wstring_view title, std::wstring_view message, std::function<void(bool)> then,
                     std::wstring_view ok, std::wstring_view cancel) {
    const auto& strings = App::Strings();
    auto dialog = std::make_unique<Dialog>();
    dialog->Title(title)
        .Message(message)
        .PrimaryButton(ok.empty() ? strings.ok : std::wstring(ok))
        .SecondaryButton(cancel.empty() ? strings.cancel : std::wstring(cancel))
        .DefaultButton(DialogCommand::Primary)
        .CancelButton(DialogCommand::Secondary)
        .OnResult([then = std::move(then)](DialogResult r) {
            if (then) then(r == DialogResult::Primary);
        });
    ShowDialog(std::move(dialog));
}

void Window::Alert(std::wstring_view title, std::wstring_view message) {
    auto dialog = std::make_unique<Dialog>();
    dialog->Title(title).Message(message).PrimaryButton(App::Strings().ok);
    ShowDialog(std::move(dialog));
}

void Window::Prompt(std::wstring_view title, std::wstring_view message,
                    std::function<void(std::optional<std::wstring>)> then,
                    std::wstring_view placeholder) {
    const auto& strings = App::Strings();
    auto dialog = std::make_unique<Dialog>();
    dialog->Title(title)
        .Message(message)
        .PrimaryButton(strings.ok)
        .SecondaryButton(strings.cancel)
        .DefaultButton(DialogCommand::Primary)
        .CancelButton(DialogCommand::Secondary);
    auto* box = &dialog->Add<TextBox>();
    box->Placeholder(placeholder);
    dialog->OnResult([then = std::move(then), box](DialogResult r) {
        if (!then) return;
        if (r == DialogResult::Primary) then(box->Text());
        else then(std::nullopt);
    });
    ShowDialog(std::move(dialog));
}

Control* Window::FocusFirst() {
    Control* c = impl_->FindFirstFocusable();
    if (c) impl_->SetFocusControl(c);
    return c;
}

Control* Window::FocusNext(bool backwards) {
    Control* c = backwards ? impl_->FindPrevFocusable(impl_->Focused())
                           : impl_->FindNextFocusable(impl_->Focused());
    if (c) impl_->SetFocusControl(c);
    return c;
}

Connection Window::OnFrame(std::function<bool(float dt)> fn) { return impl_->OnFrame(std::move(fn)); }

void Window::Icon(int resource_id) { impl_->SetIcon(resource_id); }
void Window::Icon(std::wstring_view path_or_name) { impl_->SetIcon(path_or_name); }
void Window::Icon(std::span<const std::byte> ico) { impl_->SetIconMemory(ico); }

void Window::DumpTree(std::wostream& out) const { impl_->DumpTree(out); }

void Window::RunAsync(std::function<void()> work, std::function<void()> then) {
    impl_->RunWorker(std::move(work), std::move(then));
}

void Window::RunAsync(std::function<void()> work, std::function<void()> then, std::wstring_view busy) {
    ShowBusy(busy);
    RunAsync(std::move(work), [this, then = std::move(then)] {
        CloseBusy();
        if (then) then();
    });
}

void Window::ShowToast(std::string_view utf8) { ShowToast(U8(utf8)); }

void Window::ShowFlyout(Flyout& flyout, const Control* anchor) {
    impl_->ShowFlyout(flyout, anchor);
}

void Window::ShowTeachingTip(TeachingTip& tip, const Control* anchor) {
    impl_->ShowTeachingTip(tip, anchor);
}

void Window::CloseFlyout() { impl_->CloseFlyout(); }

bool Window::FlyoutActive() const { return impl_->FlyoutActive(); }
void Window::CloseDialog() { impl_->CloseDialog(); }
void Window::ShowToast(std::wstring_view text) { impl_->ShowToast(text); }
void Window::ShowToast(std::wstring_view text, ToastKind kind) {
    ToastData data;
    data.text = std::wstring(text);
    data.kind = kind;
    impl_->ShowToast(std::move(data));
}
void Window::ShowToast(ToastData data) { impl_->ShowToast(std::move(data)); }
void Window::ToastMotion(lumen::ToastMotion motion) { impl_->SetToastMotion(motion); }
lumen::ToastMotion Window::ToastMotion() const { return impl_->GetToastMotion(); }
bool Window::DialogActive() const { return impl_->active_dialog_ != nullptr; }
void Window::Invalidate() { impl_->Invalidate(); }
void Window::LayoutNow() { impl_->LayoutNow(); }
void Window::DispatchMouseMove(Point client_dip, uint32_t buttons) {
    impl_->DispatchMouseMove(client_dip, buttons);
}
void Window::DispatchMouseDown(Point client_dip, uint32_t buttons) {
    impl_->DispatchMouseButton(client_dip, buttons, true, buttons);
}
void Window::DispatchMouseUp(Point client_dip, uint32_t buttons) {
    impl_->DispatchMouseButton(client_dip, buttons, false, buttons);
}
void Window::DispatchTouchDown(Point client_dip) { impl_->DispatchTouch(client_dip, 0); }
void Window::DispatchTouchMove(Point client_dip) { impl_->DispatchTouch(client_dip, 1); }
void Window::DispatchTouchUp(Point client_dip) { impl_->DispatchTouch(client_dip, 2); }
bool Window::DispatchKey(uint32_t vk) { return impl_->DispatchKey(vk); }
Control* Window::Hovered() const { return impl_->Hovered(); }
Control* Window::Focused() const { return impl_->Focused(); }
void* Window::NativeHandle() const { return impl_->NativeHandle(); }

void Window::ShowBusy(std::wstring_view text, std::function<void()> on_cancel) {
    impl_->ShowBusy(text, std::move(on_cancel));
}
void Window::CloseBusy() { impl_->CloseBusy(); }
bool Window::BusyActive() const { return impl_->BusyActive(); }
void Window::ShowDrawer(Drawer& drawer, Edge edge) { impl_->ShowDrawer(drawer, edge); }
void Window::CloseDrawer() { impl_->RequestCloseDrawer(); }
bool Window::DrawerActive() const { return impl_->DrawerActive(); }
void Window::Post(std::function<void()> fn) { impl_->Post(std::move(fn)); }
bool Window::IsUiThread() const { return impl_->IsUiThread(); }
Window::TimerId Window::SetInterval(float seconds, std::function<void()> fn) {
    return impl_->SetInterval(seconds, std::move(fn), false);
}
Window::TimerId Window::SetTimeout(float seconds, std::function<void()> fn) {
    return impl_->SetInterval(seconds, std::move(fn), true);
}
void Window::ClearTimer(TimerId id) { impl_->ClearTimer(id); }
void Window::BindShortcut(std::wstring_view chord, std::function<void()> fn) {
    impl_->BindShortcut(chord, std::move(fn));
}
void Window::Bind(Command& command) {
    if (command.Shortcut().empty()) return;
    impl_->BindShortcut(command.Shortcut(), [&command] {
        if (command.Enabled()) command.Execute();
    });
}
void Window::RememberPlacement(std::wstring_view registry_path) {
    impl_->RememberPlacement(registry_path);
}
void Window::TrayIcon(void* hicon, std::wstring_view tooltip) {
    impl_->TrayIcon(hicon, tooltip);
}
void Window::TrayIcon(int resource_id, std::wstring_view tooltip) {
    Icon(resource_id);
    void* icon = nullptr;
    if (HWND hwnd = static_cast<HWND>(NativeHandle())) {
        icon = reinterpret_cast<void*>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
        if (!icon) icon = reinterpret_cast<void*>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));
    }
    TrayIcon(icon, tooltip);
}
void Window::TrayIcon(std::wstring_view path_or_name, std::wstring_view tooltip) {
    Icon(path_or_name);
    void* icon = nullptr;
    if (HWND hwnd = static_cast<HWND>(NativeHandle())) {
        icon = reinterpret_cast<void*>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
        if (!icon) icon = reinterpret_cast<void*>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));
    }
    TrayIcon(icon, tooltip);
}
void Window::TrayIcon(std::span<const std::byte> ico, std::wstring_view tooltip) {
    Icon(ico);
    void* icon = nullptr;
    if (HWND hwnd = static_cast<HWND>(NativeHandle())) {
        icon = reinterpret_cast<void*>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
        if (!icon) icon = reinterpret_cast<void*>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));
    }
    TrayIcon(icon, tooltip);
}
void Window::OnTrayClick(std::function<void()> handler) { impl_->OnTrayClick(std::move(handler)); }
Connection Window::BindTrayClick(std::function<void()> handler) {
    return impl_->BindTrayClick(std::move(handler));
}
void Window::MinimizeToTray(bool on) { impl_->MinimizeToTray(on); }
void Window::TrayMenu(Menu menu) { impl_->SetTrayMenu(std::move(menu)); }

WindowImpl::WindowImpl(Window* api, std::wstring_view title, Size client_size, Frame frame)
    : api_(api), frame_(frame), title_(title), glow_intensity_(0.5f) {
    ui_thread_id_ = GetCurrentThreadId();
    QueryPerformanceFrequency(&qpc_freq_);
    root_ = std::make_unique<StackPanel>();
    root_->window_ = api_;
    if (frame_ == Frame::Client) {
        title_bar_ = std::make_unique<TitleBar>();
        title_bar_->window_ = api_;
        title_bar_->title_ = title_;
    }
    EnsureWindowClass();
    scale_ = static_cast<float>(GetDpiForSystem()) / 96.0f;
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        UINT dpi_x = 96, dpi_y = 96;
        if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
            scale_ = static_cast<float>(dpi_x) / 96.0f;
        }
    }

    const DWORD style = FrameStyle();
    const DWORD ex_style = WS_EX_NOREDIRECTIONBITMAP;
    RECT rect{0, 0, static_cast<LONG>(client_size.w * scale_),
              static_cast<LONG>(client_size.h * scale_)};
    AdjustFrameRect(&rect);
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (frame_ == Frame::Client && GetCursorPos(&cursor)) {
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor)) {
            x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
            y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
        }
    }
    hwnd_ = CreateWindowExW(ex_style, L"lumen_window", title_.c_str(), style, x, y, width, height,
                            nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd_) ++g_live_windows;
    if (hwnd_) {
        scale_ = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
        AppBindWindow(hwnd_);
    }
    if (title_bar_) BindWindowRecursive(title_bar_.get(), api_);
    UpdateClientSize();
    RefreshTheme();
    renderer_.Init(hwnd_, client_w_, client_h_);
    ApplyClientChrome();
    RegisterOleDrop();
}

WindowImpl::~WindowImpl() {
    keep_alive_.reset();
    UiaShutdown();
    if (active_busy_) CloseBusy();
    if (active_drawer_) FinishDrawer();
    if (active_dialog_) FinishDialog();
    if (active_flyout_) CloseFlyout(false);
    RemoveTray();
    UnregisterOleDrop();
    renderer_.Shutdown();
    if (hwnd_) DestroyWindow(hwnd_);
}

void WindowImpl::EnsureWindowClass() {
    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &WindowImpl::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"lumen_window";
        const ATOM atom = RegisterClassExW(&wc);
        EnableMouseInPointer(TRUE);
        return atom != 0;
    }();
    (void)registered;
}

LRESULT CALLBACK WindowImpl::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    return self->Handle(hwnd, msg, wparam, lparam);
}

LRESULT WindowImpl::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!hwnd_) hwnd_ = hwnd;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        EndPaint(hwnd_, &ps);
        Paint();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCCALCSIZE:
        if (frame_ != Frame::Client) break;
        if (wparam && IsZoomed(hwnd)) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            const UINT dpi = GetDpiForWindow(hwnd);
            const int frame_x = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                                GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const int frame_y = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                                GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            params->rgrc[0].left += frame_x;
            params->rgrc[0].right -= frame_x;
            params->rgrc[0].top += frame_y;
            params->rgrc[0].bottom -= frame_y;
        }
        return 0;
    case WM_NCPAINT:
        if (frame_ == Frame::Client) return 0;
        break;
    case WM_NCACTIVATE:
        if (frame_ != Frame::Client) break;
        // lParam = -1：激活记账交给系统，但不画默认非客户区（否则无边框会闪系统边）。
        DefWindowProcW(hwnd, WM_NCACTIVATE, wparam, static_cast<LPARAM>(-1));
        Invalidate();
        return TRUE;
    case WM_NCHITTEST:
        if (frame_ != Frame::Client) break;
        return HitTestFrame(lparam);
    case WM_NCMOUSEMOVE:
        if (frame_ != Frame::Client) break;
        {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            const Point dip{static_cast<float>(pt.x) / scale_, static_cast<float>(pt.y) / scale_};
            LRESULT hit = 0;
            if (title_bar_ && dip.y >= 0.0f && dip.y < CaptionHeight()) {
                switch (title_bar_->Hit(dip)) {
                case TitleBar::Region::Min: hit = HTMINBUTTON; break;
                case TitleBar::Region::Max: hit = HTMAXBUTTON; break;
                case TitleBar::Region::Close: hit = HTCLOSE; break;
                default: break;
                }
            }
            SetCaptionHover(hit);
            TrackNcMouse();
        }
        return 0;
    case WM_NCMOUSELEAVE:
        tracking_nc_mouse_ = false;
        SetCaptionHover(0);
        return 0;
    case WM_NCLBUTTONDOWN:
        if (frame_ != Frame::Client) break;
        if (wparam == HTMINBUTTON) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        if (wparam == HTMAXBUTTON) {
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        if (wparam == HTCLOSE) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (frame_ == Frame::Client && wparam == HTCAPTION) {
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE:
        in_size_move_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        in_size_move_ = false;
        Invalidate();
        return 0;
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED && minimize_to_tray_ && tray_installed_) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (wparam != SIZE_MINIMIZED) {
            UpdateClientSize();
            backdrop_cache_.reset();
            backdrop_cache_dirty_ = true;
            RequestRelayout();
            // ResizeBuffers 会丢掉旧帧；NOREDIRECTIONBITMAP 窗口必须当消息内 Present，
            // 否则 DWM 合成到空缓冲，拖动时内容区整片空白。
            if (painting_) paint_again_ = true;
            else Paint();
        }
        return 0;
    case WM_DPICHANGED: {
        scale_ = static_cast<float>(HIWORD(wparam)) / 96.0f;
        backdrop_cache_.reset();
        backdrop_cache_dirty_ = true;
        painter_.InvalidateAcrylic();
        auto* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested) {
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        UpdateClientSize();
        RequestRelayout();
        Invalidate();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        if (min_size_dip_.w > 0.0f && min_size_dip_.h > 0.0f) {
            RECT rect{0, 0, static_cast<LONG>(min_size_dip_.w * scale_),
                      static_cast<LONG>(min_size_dip_.h * scale_)};
            AdjustFrameRect(&rect);
            info->ptMinTrackSize.x = rect.right - rect.left;
            info->ptMinTrackSize.y = rect.bottom - rect.top;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (LegacyMouseFromPointer()) return 0;
        OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam));
        return 0;
    case WM_MOUSELEAVE:
        if (LegacyMouseFromPointer()) return 0;
        tracking_mouse_ = false;
        toast_press_ = -1;
        ClearToastHover();
        if (hovered_) {
            hovered_->OnMouseLeave();
            hovered_ = nullptr;
        }
        SyncSpotlights({-1.0f, -1.0f}, false);
        HideTooltip(true);
        tooltip_control_ = nullptr;
        tooltip_suppressed_ = nullptr;
        tooltip_custom_ = nullptr;
        tooltip_hover_ = nullptr;
        return 0;
    case WM_LBUTTONDOWN:
        if (LegacyMouseFromPointer()) return 0;
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      true, MK_LBUTTON);
        return 0;
    case WM_RBUTTONDOWN:
        if (LegacyMouseFromPointer()) return 0;
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      true, MK_RBUTTON);
        return 0;
    case WM_MBUTTONDOWN:
        if (LegacyMouseFromPointer()) return 0;
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      true, MK_MBUTTON);
        return 0;
    case WM_LBUTTONUP:
        if (LegacyMouseFromPointer()) return 0;
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      false, MK_LBUTTON);
        return 0;
    case WM_RBUTTONUP:
        if (LegacyMouseFromPointer()) return 0;
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      false, MK_RBUTTON);
        return 0;
    case WM_MBUTTONUP:
        if (LegacyMouseFromPointer()) return 0;
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      false, MK_MBUTTON);
        return 0;
    case WM_LBUTTONDBLCLK: {
        Point p{static_cast<float>(GET_X_LPARAM(lparam)) / scale_,
                static_cast<float>(GET_Y_LPARAM(lparam)) / scale_};
        if (Control* hit = HitTest(p)) hit->OnMouseDoubleClick(WindowImpl::ToLocal(hit, p));
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (LegacyMouseFromPointer()) return 0;
        HideTooltip();
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &screen);
        Point p{static_cast<float>(screen.x) / scale_, static_cast<float>(screen.y) / scale_};
        const float delta = static_cast<short>(HIWORD(wparam)) / float(WHEEL_DELTA);
        for (Control* hit = HitTest(p); hit; hit = hit->parent_) {
            if (hit->OnWheel(delta)) break;
        }
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        if (LegacyMouseFromPointer()) return 0;
        HideTooltip();
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &screen);
        Point p{static_cast<float>(screen.x) / scale_, static_cast<float>(screen.y) / scale_};
        const float delta = static_cast<short>(HIWORD(wparam)) / float(WHEEL_DELTA);
        for (Control* hit = HitTest(p); hit; hit = hit->parent_) {
            if (hit->OnHWheel(delta)) break;
        }
        return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERLEAVE:
    case WM_POINTERCAPTURECHANGED:
    case WM_POINTERWHEEL:
    case WM_POINTERHWHEEL:
        if (OnPointer(msg, wparam, lparam)) return 0;
        break;
    case WM_CAPTURECHANGED:
        toast_press_ = -1;
        if (captured_) {
            Control* target = captured_;
            captured_ = nullptr;
            target->OnMouseUp({-1.0f, -1.0f}, 0);
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (OnKeyDown(static_cast<uint32_t>(wparam))) return 0;
        break;
    case WM_CHAR:
        // 行内组字期间拼音由 WM_IME_COMPOSITION 更新，再走 OnChar 会把拉丁字母写进文本。
        if (focused_ && !focused_->ImeComposing()) {
            focused_->OnChar(static_cast<wchar_t>(wparam));
        }
        return 0;
    case WM_IME_CHAR:
        if (focused_ && focused_->ImeInline()) return 0;
        break;
    case WM_IME_SETCONTEXT: {
        if (focused_ && focused_->ImeInline()) {
            lparam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
        }
        const LRESULT r = DefWindowProcW(hwnd, msg, wparam, lparam);
        SyncImeCaret();
        return r;
    }
    case WM_IME_STARTCOMPOSITION:
        SyncImeCaret();
        // DefWindowProc 会造系统组字窗（拼音浮在框外那块白底）。行内组字必须吃掉。
        if (focused_ && focused_->ImeInline()) return 0;
        break;
    case WM_IME_COMPOSITION:
        if (focused_ && focused_->ImeInline()) {
            HandleImeComposition(lparam);
            return 0;
        }
        SyncImeCaret();
        break;
    case WM_IME_ENDCOMPOSITION:
        if (focused_ && focused_->ImeInline()) {
            focused_->OnImeEnd();
            SyncImeCaret();
            return 0;
        }
        break;
    case WM_IME_NOTIFY:
        if (wparam == IMN_OPENCANDIDATE || wparam == IMN_CHANGECANDIDATE) {
            SyncImeCaret();
        }
        break;
    case WM_IME_REQUEST: {
        LRESULT result = 0;
        if (OnImeRequest(wparam, lparam, &result)) return result;
        break;
    }
    case WM_SETFOCUS:
        SyncImeCaret();
        break;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            CursorShape shape = CursorShape::Arrow;
            POINT sp{};
            GetCursorPos(&sp);
            ScreenToClient(hwnd_, &sp);
            const Point p{static_cast<float>(sp.x) / scale_, static_cast<float>(sp.y) / scale_};
            ptrdiff_t toast_i = -1;
            const ToastPart toast_part = HitToast(p, &toast_i);
            if (toast_part == ToastPart::Action || toast_part == ToastPart::Close) {
                shape = CursorShape::Hand;
            } else if (!tooltip_close_.IsEmpty() && tooltip_close_.Contains(p)) {
                shape = CursorShape::Hand;
            } else if (tooltip_hover_) {
                shape = tooltip_hover_->CursorAt(ToLocal(tooltip_hover_, p));
            } else if (hovered_) {
                shape = hovered_->CursorAt(ToLocal(hovered_, p));
            }
            switch (shape) {
            case CursorShape::IBeam: SetCursor(LoadCursorW(nullptr, IDC_IBEAM)); break;
            case CursorShape::Hand: SetCursor(LoadCursorW(nullptr, IDC_HAND)); break;
            case CursorShape::SizeWE: SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); break;
            case CursorShape::SizeNS: SetCursor(LoadCursorW(nullptr, IDC_SIZENS)); break;
            default: SetCursor(LoadCursorW(nullptr, IDC_ARROW)); break;
            }
            return TRUE;
        }
        break;
    case WM_GETOBJECT:
        return UiaGetObject(wparam, lparam);
    case WM_CLOSE:
        if (closing_ && !closing_()) return 0;
        SavePlacement();
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        UiaShutdown();
        if (active_busy_) CloseBusy();
        if (active_drawer_) FinishDrawer();
        if (active_dialog_) FinishDialog();
        if (active_flyout_) CloseFlyout(false);
        RemoveTray();
        AppUnbindWindow(hwnd_);
        UnregisterOleDrop();
        renderer_.Shutdown();   // 先于 hwnd_ 清空，DComp Target 仍绑着有效 HWND
        closed_ = true;
        animating_ = false;
        hwnd_ = nullptr;
        if (--g_live_windows <= 0) PostQuitMessage(0);   // 最后一个窗口已关，让 App::Run 返回
        return 0;
    case WM_TIMER:
        FireTimer(static_cast<UINT_PTR>(wparam));
        return 0;
    case WM_SETTINGCHANGE:
        RefreshTheme();
        RequestRelayout();
        return 0;
    case WM_SYSCOMMAND:
        if (minimize_to_tray_ && tray_installed_ && (wparam & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    default:
        if (msg == kWmPost) {
            DrainPosted();
            return 0;
        }
        if (msg == kWmTray) {
            const UINT notify = static_cast<UINT>(lparam);
            if (notify == WM_LBUTTONUP || notify == NIN_SELECT) {
                if (!tray_click_.Empty()) tray_click_.Emit();
                else RestoreFromTray();
                return 0;
            }
            if (notify == WM_RBUTTONUP || notify == WM_CONTEXTMENU) {
                if (has_tray_menu_ && api_) {
                    POINT pt{};
                    GetCursorPos(&pt);
                    ScreenToClient(hwnd, &pt);
                    tray_menu_.Popup(*api_, {static_cast<float>(pt.x) / scale_,
                                            static_cast<float>(pt.y) / scale_});
                }
                return 0;
            }
            return 0;
        }
        if (UINT activate = AppActivateMsg(); activate && msg == activate) {
            RestoreFromTray();
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WindowImpl::UpdateClientSize() {
    RECT client{};
    if (GetClientRect(hwnd_, &client)) {
        client_w_ = client.right - client.left;
        client_h_ = client.bottom - client.top;
    }
}

void WindowImpl::RefreshTheme() {
    theme_ = MakeTheme(glow_intensity_);
    backdrop_cache_dirty_ = true;
    Invalidate();
}

void WindowImpl::Show() {
    ApplyPlacement();
    ShowWindow(hwnd_, SW_SHOW);
    if (hwnd_) AppBindWindow(hwnd_);
    Invalidate();
}

void WindowImpl::Close() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void WindowImpl::Title(std::wstring_view text) {
    title_ = std::wstring(text);
    if (hwnd_) SetWindowTextW(hwnd_, title_.c_str());
    if (title_bar_) title_bar_->Title(title_);
    if (frame_ == Frame::Client) Invalidate();
}

void WindowImpl::Resize(Size client_size) {
    RECT rect{0, 0, static_cast<LONG>(client_size.w * scale_),
              static_cast<LONG>(client_size.h * scale_)};
    AdjustFrameRect(&rect);
    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WindowImpl::MinSize(Size min_size) {
    min_size_dip_ = min_size;
}

void WindowImpl::Invalidate() {
    dirty_full_ = true;
    dirty_count_ = 0;
    RequestPaint();
}

void WindowImpl::InvalidateRegion(const Rect& dip) {
    if (dirty_full_) {
        RequestPaint();
        return;
    }
    AddDirtyRect(dip);
    RequestPaint();
}

void WindowImpl::RequestPaint() {
    if (painting_) {
        paint_again_ = true;
        return;
    }
    if (!hwnd_) return;
    if (dirty_full_ || dirty_count_ <= 0) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    RECT union_px = DirtyPixelRect(dirty_rects_[0]);
    for (int i = 1; i < dirty_count_; ++i) {
        const RECT p = DirtyPixelRect(dirty_rects_[i]);
        if (p.left < union_px.left) union_px.left = p.left;
        if (p.top < union_px.top) union_px.top = p.top;
        if (p.right > union_px.right) union_px.right = p.right;
        if (p.bottom > union_px.bottom) union_px.bottom = p.bottom;
    }
    InvalidateRect(hwnd_, &union_px, FALSE);
}

RECT WindowImpl::DirtyPixelRect(const Rect& dip) const noexcept {
    const float s = scale_ > 0.0f ? scale_ : 1.0f;
    RECT px{};
    px.left = static_cast<LONG>(std::floor(dip.x * s));
    px.top = static_cast<LONG>(std::floor(dip.y * s));
    px.right = static_cast<LONG>(std::ceil(dip.Right() * s));
    px.bottom = static_cast<LONG>(std::ceil(dip.Bottom() * s));
    if (px.left < 0) px.left = 0;
    if (px.top < 0) px.top = 0;
    if (px.right > client_w_) px.right = client_w_;
    if (px.bottom > client_h_) px.bottom = client_h_;
    return px;
}

void WindowImpl::AddDirtyRect(Rect dip) {
    if (dirty_full_ || dip.IsEmpty()) return;
    const float w = client_w_ / (scale_ > 0.0f ? scale_ : 1.0f);
    const float h = client_h_ / (scale_ > 0.0f ? scale_ : 1.0f);
    dip = dip.Intersect({0.0f, 0.0f, w, h});
    if (dip.IsEmpty()) return;
    for (int i = 0; i < dirty_count_; ++i) {
        if (!dirty_rects_[i].Intersect(dip).IsEmpty()) {
            dirty_rects_[i] = UnionRect(dirty_rects_[i], dip);
            return;
        }
    }
    if (dirty_count_ < kMaxDirtyRects) {
        dirty_rects_[dirty_count_++] = dip;
        return;
    }
    Rect box = dip;
    for (int i = 0; i < dirty_count_; ++i) box = UnionRect(box, dirty_rects_[i]);
    dirty_rects_[0] = box;
    dirty_count_ = 1;
}

void WindowImpl::RequestRelayout() {
    layout_dirty_ = true;
    Invalidate();
}

void WindowImpl::RequestAnimation(Control* control) {
    if (!hwnd_) return;
    if (control && !control->anim_listed_) {
        control->anim_listed_ = true;
        anim_targets_.push_back(control);
    }
    if (!animating_) {
        QueryPerformanceCounter(&last_tick_);
        animating_ = true;
    }
    if (control) control->Invalidate();
    else Invalidate();
}

Connection WindowImpl::OnFrame(std::function<bool(float)> fn) {
    if (!fn) return {};
    const uint64_t id = next_frame_id_++;
    frame_cbs_.push_back(FrameCb{id, std::move(fn)});
    RequestAnimation(nullptr);
    return Connection(
        [](void* p, uint64_t i) { static_cast<WindowImpl*>(p)->DisconnectFrame(i); }, this, id);
}

void WindowImpl::DisconnectFrame(uint64_t id) {
    if (id == 0) return;
    for (auto it = frame_cbs_.begin(); it != frame_cbs_.end(); ++it) {
        if (it->id == id) {
            frame_cbs_.erase(it);
            return;
        }
    }
}

void WindowImpl::LoadFirstExeIcon() {
    struct First {
        WindowImpl* self = nullptr;
        bool taken = false;
    } ctx{this, false};
    EnumResourceNamesW(
        GetModuleHandleW(nullptr), RT_GROUP_ICON,
        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL {
            auto* ctx = reinterpret_cast<First*>(param);
            if (!ctx || !ctx->self || ctx->taken) return FALSE;
            ctx->taken = true;
            if (IS_INTRESOURCE(name)) {
                ctx->self->SetIcon(static_cast<int>(reinterpret_cast<uintptr_t>(name)));
            } else {
                ctx->self->SetIcon(std::wstring_view(name));
            }
            return FALSE;
        },
        reinterpret_cast<LONG_PTR>(&ctx));
}

void WindowImpl::SetIconMemory(std::span<const std::byte> ico) {
    if (ico.empty() || !hwnd_) return;
    auto make = [&](int cx, int cy) -> HICON {
        return CreateIconFromResourceEx(const_cast<PBYTE>(reinterpret_cast<const BYTE*>(ico.data())),
                                        static_cast<DWORD>(ico.size()), TRUE, 0x00030000, cx, cy,
                                        LR_DEFAULTCOLOR);
    };
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(make(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(
                     make(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))));
    if (title_bar_) title_bar_->LoadIconMemory(ico);
}

void WindowImpl::SetIcon(int resource_id) {
    HMODULE mod = GetModuleHandleW(nullptr);
    if (HRSRC src = FindResourceW(mod, MAKEINTRESOURCEW(resource_id), RT_RCDATA)) {
        if (HGLOBAL mem = LoadResource(mod, src)) {
            const DWORD bytes = SizeofResource(mod, src);
            if (const void* data = LockResource(mem); data && bytes > 0) {
                SetIconMemory({static_cast<const std::byte*>(data), static_cast<size_t>(bytes)});
                return;
            }
        }
    }
    if (!hwnd_) return;
    auto load = [&](int cx, int cy) -> HICON {
        return reinterpret_cast<HICON>(LoadImageW(mod, MAKEINTRESOURCEW(resource_id), IMAGE_ICON, cx,
                                                  cy, LR_DEFAULTCOLOR));
    };
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(
                     load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))));
}

void WindowImpl::SetIcon(std::wstring_view path_or_name) {
    if (path_or_name.empty()) return;
    std::wstring name(path_or_name);
    const bool file = name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos ||
                      (name.size() >= 4 && (name.ends_with(L".ico") || name.ends_with(L".ICO")));
    HMODULE mod = file ? nullptr : GetModuleHandleW(nullptr);
    auto load = [&](int cx, int cy) -> HICON {
        return reinterpret_cast<HICON>(LoadImageW(mod, name.c_str(), IMAGE_ICON, cx, cy,
                                                  file ? LR_LOADFROMFILE | LR_DEFAULTCOLOR
                                                       : LR_DEFAULTCOLOR));
    };
    if (!hwnd_) return;
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(load(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(
                     load(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))));
}

namespace {
void DumpControl(std::wostream& out, Control* node, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; ++i) out << L"  ";
    const char* raw = typeid(*node).name();
    wchar_t wide[256]{};
    MultiByteToWideChar(CP_ACP, 0, raw, -1, wide, 256);
    const Rect& r = node->AbsoluteBounds();
    out << wide << L" vis=" << (node->Visible() ? 1 : 0) << L" en=" << (node->Enabled() ? 1 : 0)
        << L" focus=" << (node->HasFocus() ? 1 : 0) << L" [" << r.x << L"," << r.y << L" "
        << r.w << L"x" << r.h << L"]\n";
    if (Panel* panel = node->AsPanel()) {
        for (size_t i = 0; i < panel->ChildCount(); ++i) DumpControl(out, &panel->Child(i), indent + 1);
    }
}
} // namespace

void WindowImpl::DumpTree(std::wostream& out) const {
    out << L"Window\n";
    if (title_bar_) DumpControl(out, title_bar_.get(), 1);
    if (root_) DumpControl(out, root_.get(), 1);
}

void WindowImpl::RunWorker(std::function<void()> work, std::function<void()> then) {
    auto alive = std::weak_ptr<void>(keep_alive_);
    std::thread([this, work = std::move(work), then = std::move(then), alive]() mutable {
        if (work) work();
        if (alive.expired()) return;
        Post([then = std::move(then), alive] {
            if (alive.expired()) return;
            if (then) then();
        });
    }).detach();
}

LumaTextBridge* WindowImpl::LumaOf(Window* window) {
    if (!window) return nullptr;
    return window->Impl()->renderer_.Luma();
}

bool WindowImpl::HitTestBody(Window* window, std::wstring_view text, float x_dip, size_t* index,
                             TextRole role) {
    if (!window || !index) return false;
    WindowImpl* impl = window->Impl();
    LumaTextBridge* luma = impl->renderer_.Luma();
    if (!luma || !luma->Enabled()) return false;
    IDWriteTextFormat* format = UiText().Format(role);
    return luma->HitTestPoint(text, format, impl->scale_, x_dip, index);
}

bool WindowImpl::CaretXBody(Window* window, std::wstring_view text, size_t index, float* x_dip,
                            TextRole role) {
    if (!window || !x_dip) return false;
    WindowImpl* impl = window->Impl();
    LumaTextBridge* luma = impl->renderer_.Luma();
    if (!luma || !luma->Enabled()) return false;
    IDWriteTextFormat* format = UiText().Format(role);
    return luma->PositionToX(text, format, impl->scale_, index, x_dip);
}


void WindowImpl::GlowIntensity(float intensity) {
    if (glow_intensity_ == intensity) return;
    glow_intensity_ = Clamp(intensity, 0.0f, 1.0f);
    RefreshTheme();
}

void WindowImpl::SetBackdrop(Backdrop backdrop) {
    if (backdrop_ == backdrop) return;
    backdrop_ = backdrop;
    backdrop_cache_.reset();
    backdrop_cache_dirty_ = true;
    Invalidate();
}

void WindowImpl::DrawBackdrop(const Rect& client) {
    if (backdrop_ == Backdrop::None) return;
    if (backdrop_ == Backdrop::Grid || backdrop_ == Backdrop::All) {
        constexpr float kCell = 36.0f;
        const Color line = theme_.grid_line;
        for (float x = kCell; x < client.w; x += kCell) {
            painter_.DrawLine({x, 0.0f}, {x, client.h}, line);
        }
        for (float y = kCell; y < client.h; y += kCell) {
            painter_.DrawLine({0.0f, y}, {client.w, y}, line);
        }
    }
    if (backdrop_ == Backdrop::Vignette || backdrop_ == Backdrop::All) {
        // 顶部中心环境辉光，向下衰减到透明（黑底上等效于参考页的 radial vignette）。
        painter_.FillRectRadial(client, {client.w * 0.5f, client.h * 0.15f}, client.w * 0.9f,
                                theme_.ambient_flare,
                                Color{theme_.ambient_flare.r, theme_.ambient_flare.g,
                                      theme_.ambient_flare.b, 0.0f}, 0.7f);
    }
}









void WindowImpl::UpdatePerfHud(float draw_ms, float present_ms, float frame_ms, bool full_present,
                               UINT dirty_n) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (last_hud_qpc_.QuadPart != 0 && qpc_freq_.QuadPart != 0) {
        const float dt_ms = 1000.0f *
                            static_cast<float>(now.QuadPart - last_hud_qpc_.QuadPart) /
                            static_cast<float>(qpc_freq_.QuadPart);
        if (dt_ms > 0.05f) {
            const float fps = 1000.0f / dt_ms;
            fps_ema_ = fps_ema_ <= 0.0f ? fps : fps_ema_ * 0.9f + fps * 0.1f;
        }
    }
    last_hud_qpc_ = now;
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    float mb = 0.0f;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        mb = static_cast<float>(memory.WorkingSetSize) / (1024.0f * 1024.0f);
    }
    wchar_t buf[96];
    if (full_present) {
        swprintf_s(buf, L"%.1f FPS  ·  %.2f ms  ·  D %.2f  ·  P %.2f  ·  full  ·  %.0f MB",
                   fps_ema_, frame_ms, draw_ms, present_ms, mb);
    } else {
        swprintf_s(buf,
                   L"%.1f FPS  ·  %.2f ms  ·  D %.2f  ·  P %.2f  ·  %u dirty  ·  %.0f MB",
                   fps_ema_, frame_ms, draw_ms, present_ms, dirty_n, mb);
    }
    if (wcscmp(perf_hud_, buf) != 0) {
        wcscpy_s(perf_hud_, buf);
        // HUD 数字几乎每帧都变。TitleBar::Status 会 Invalidate，Paint 期间变成
        // paint_again_，Client 窗空闲永远 60fps。只写文案，等下一次真正需要画的帧。
        if (title_bar_) title_bar_->status_ = perf_hud_;
    }
}

void WindowImpl::BlitBackdrop(ID2D1DeviceContext2* dc, const Rect& client) {
    if (backdrop_ == Backdrop::None || !dc) {
        painter_.FillRect(client, theme_.bg);
        return;
    }
    const bool size_ok = backdrop_cache_ && backdrop_cache_->GetPixelSize().width ==
                                                static_cast<UINT32>(client_w_) &&
                         backdrop_cache_->GetPixelSize().height == static_cast<UINT32>(client_h_);
    if (!size_ok || backdrop_cache_dirty_) {
        backdrop_cache_.reset();
        D2D1_BITMAP_PROPERTIES1 props{};
        props.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        const D2D1_SIZE_U size{static_cast<UINT32>(client_w_), static_cast<UINT32>(client_h_)};
        if (SUCCEEDED(dc->CreateBitmap(size, nullptr, 0, &props, &backdrop_cache_))) {
            ComPtr<ID2D1Image> previous;
            dc->GetTarget(&previous);
            if (previous) {
                dc->SetTarget(backdrop_cache_.get());
                painter_.FillRect(client, theme_.bg);
                DrawBackdrop(client);
                dc->SetTarget(previous.get());
                backdrop_cache_dirty_ = false;
            } else {
                backdrop_cache_.reset();
                painter_.FillRect(client, theme_.bg);
                DrawBackdrop(client);
                return;
            }
        } else {
            painter_.FillRect(client, theme_.bg);
            DrawBackdrop(client);
            return;
        }
    }
    painter_.DrawBitmap(backdrop_cache_.get(), client);
}


void WindowImpl::BindWindowRecursive(Control* tree, Window* window) {
    // 弹层/对话框常在入树前 Add 子控件；主树挂载脱离构建的子树时也走这里。
    if (!tree) return;
    std::vector<Control*> stack{tree};
    while (!stack.empty()) {
        Control* node = stack.back();
        stack.pop_back();
        if (!node) continue;
        node->window_ = window;
        if (node->tooltip_content_) stack.push_back(node->tooltip_content_.get());
        if (auto* panel = node->AsPanel()) {
            for (size_t i = 0; i < panel->ChildCount(); ++i) {
                stack.push_back(&panel->Child(i));
            }
        }
    }
}

void WindowImpl::RegisterOleDrop() {
    if (ole_drop_ || !hwnd_) return;
    if (FAILED(OleInitialize(nullptr))) return;
    ole_drop_ = new OleDropTarget(this);
    if (FAILED(RegisterDragDrop(hwnd_, ole_drop_))) {
        ole_drop_->Release();
        ole_drop_ = nullptr;
    }
}

void WindowImpl::UnregisterOleDrop() {
    if (!ole_drop_) return;
    if (hwnd_) RevokeDragDrop(hwnd_);
    ole_drop_->Detach();
    ole_drop_->Release();
    ole_drop_ = nullptr;
    SetDropArmed(nullptr);
}

Point WindowImpl::DipFromScreen(long screen_x, long screen_y) const {
    POINT pt{screen_x, screen_y};
    if (hwnd_) ScreenToClient(hwnd_, &pt);
    const float scale = scale_ > 0.0f ? scale_ : 1.0f;
    return {static_cast<float>(pt.x) / scale, static_cast<float>(pt.y) / scale};
}

Control* WindowImpl::FileDropAt(Point client_dip) {
    for (Control* node = HitTest(client_dip); node; node = node->parent_) {
        if (node->AcceptsFileDrop()) return node;
    }
    return nullptr;
}

Control* WindowImpl::TextDropAt(Point client_dip) {
    for (Control* node = HitTest(client_dip); node; node = node->parent_) {
        if (node->AcceptsTextDrop()) return node;
    }
    return nullptr;
}

void WindowImpl::SetDropArmed(Control* zone) {
    if (drop_armed_ == zone) return;
    if (drop_armed_) drop_armed_->OnFileDrag(false);
    drop_armed_ = zone;
    if (drop_armed_) drop_armed_->OnFileDrag(true);
}

void WindowImpl::ForgetTree(Control* tree) {
    if (!tree) return;
    std::vector<Control*> stack{tree};
    while (!stack.empty()) {
        Control* node = stack.back();
        stack.pop_back();
        if (!node) continue;
        // 窗口只会留存挂载过的控件指针；window_ 为空说明从未入树。
        if (node->window_) node->window_->Impl()->ForgetControl(node);
        if (node->tooltip_content_) stack.push_back(node->tooltip_content_.get());
        if (auto* panel = node->AsPanel()) {
            for (size_t i = 0; i < panel->ChildCount(); ++i) {
                stack.push_back(&panel->Child(i));
            }
        }
    }
}




void WindowImpl::ForgetControl(const Control* control) {
    UiaForget(control);
    if (hovered_ == control) hovered_ = nullptr;
    if (captured_ == control) {
        captured_ = nullptr;
        // 弹层可在 OnMouseDown 内立即关闭。只清内部指针会遗留 Win32 capture，
        // 随后的非客户区点击（尤其自绘标题栏关闭按钮）将无法到达窗口。
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
    }
    if (focused_ == control) focused_ = nullptr;
    if (tooltip_control_ == control) {
        tooltip_control_ = nullptr;
        tooltip_custom_ = nullptr;
        if (tooltip_hover_) tooltip_hover_ = nullptr;
        HideTooltip(true);
    }
    if (tooltip_suppressed_ == control) tooltip_suppressed_ = nullptr;
    if (tooltip_custom_ == control) {
        tooltip_custom_ = nullptr;
        tooltip_hover_ = nullptr;
        HideTooltip(true);
    }
    if (tooltip_hover_ == control) tooltip_hover_ = nullptr;
    if (flyout_anchor_ == control) flyout_anchor_ = nullptr;
    if (dialog_focus_return_ == control) dialog_focus_return_ = nullptr;
    if (busy_focus_return_ == control) busy_focus_return_ = nullptr;
    if (drawer_focus_return_ == control) drawer_focus_return_ = nullptr;
    if (active_dialog_ == control) active_dialog_ = nullptr;
    if (active_busy_ == control) active_busy_ = nullptr;
    if (active_drawer_ == control) active_drawer_ = nullptr;
    if (active_flyout_ == control) {
        active_flyout_ = nullptr;
        flyout_anchor_ = nullptr;
        flyout_closed_ = {};
        flyout_focus_return_ = nullptr;
    }
    if (drop_armed_ == control) drop_armed_ = nullptr;
    if (control && control->anim_listed_) {
        control->anim_listed_ = false;
        anim_targets_.erase(std::remove(anim_targets_.begin(), anim_targets_.end(), control),
                            anim_targets_.end());
    }
}
















void WindowImpl::Post(std::function<void()> fn) {
    if (!fn) return;
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        post_queue_.push_back(std::move(fn));
    }
    if (hwnd_) PostMessageW(hwnd_, kWmPost, 0, 0);
}

bool WindowImpl::IsUiThread() const noexcept {
    return ui_thread_id_ != 0 && GetCurrentThreadId() == ui_thread_id_;
}

void WindowImpl::DrainPosted() {
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        batch.swap(post_queue_);
    }
    for (auto& fn : batch) {
        if (fn) fn();
    }
}




void WindowImpl::BindShortcut(std::wstring_view chord, std::function<void()> fn) {
    const std::wstring key(chord);
    shortcuts_.erase(std::remove_if(shortcuts_.begin(), shortcuts_.end(),
                                    [&](const Shortcut& s) { return s.chord == key; }),
                     shortcuts_.end());
    if (fn) shortcuts_.push_back(Shortcut{key, std::move(fn)});
}

void WindowImpl::RememberPlacement(std::wstring_view registry_path) {
    placement_key_ = std::wstring(registry_path);
    if (hwnd_ && IsWindowVisible(hwnd_)) ApplyPlacement();
}

void WindowImpl::ApplyPlacement() {
    if (placement_key_.empty() || !hwnd_) return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    DWORD size = sizeof(wp);
    DWORD type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, placement_key_.c_str(), L"placement", RRF_RT_REG_BINARY,
                     &type, &wp, &size) != ERROR_SUCCESS ||
        size != sizeof(wp)) {
        return;
    }
    wp.length = sizeof(wp);
    const RECT& rc = wp.rcNormalPosition;
    HMONITOR monitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (!monitor) {
        monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;
            wp.rcNormalPosition.left = info.rcWork.left + 40;
            wp.rcNormalPosition.top = info.rcWork.top + 40;
            wp.rcNormalPosition.right = wp.rcNormalPosition.left + std::max(w, 200);
            wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + std::max(h, 160);
            wp.showCmd = SW_SHOWNORMAL;
        }
    }
    SetWindowPlacement(hwnd_, &wp);
}

void WindowImpl::SavePlacement() {
    if (placement_key_.empty() || !hwnd_) return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd_, &wp)) return;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, placement_key_.c_str(), 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, L"placement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp),
                   sizeof(wp));
    RegCloseKey(key);
}









double WindowImpl::clock_seconds() const {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}

















// 推进提示的淡入/淡出；返回本帧绘制透明度（0 = 不画）。
// 仅在 dwell / 淡入 / 淡出期间要时钟；完全显示后停拍，禁止空闲 60fps。


void WindowImpl::Layout() {
    layout_dirty_ = false;
    const float w = client_w_ / scale_;
    const float h = client_h_ / scale_;
    const float chrome = CaptionHeight();
    const float content_h = std::max(0.0f, h - chrome);
    if (title_bar_) {
        title_bar_->Measure({w, chrome}, theme_);
        title_bar_->Arrange({0.0f, 0.0f, w, chrome});
    }
    Control& root_control = *root_;
    const Theme root_theme = root_control.EffectiveTheme(theme_);
    root_control.Measure({w, content_h}, root_theme);
    root_control.Arrange({0.0f, chrome, w, content_h});
    if (active_drawer_) {
        const float pw = active_drawer_->PanelWidth();
        const float t = Clamp(active_drawer_->slide_.Value(), 0.0f, 1.0f);
        active_drawer_->Measure({pw, content_h}, theme_);
        const float x = active_drawer_->Side() == Edge::Left ? (t - 1.0f) * pw : w - t * pw;
        active_drawer_->Arrange({x, chrome, pw, content_h});
    }
    if (active_dialog_) {
        // 直调虚 Measure 不写 desired_（那是 MeasureChildAt 的职责），必须接返回值；
        // 高度按内容自适应，钳在 120..视口内。
        const Size dialog_desired = active_dialog_->Measure({w, content_h}, theme_);
        const float dialog_w = std::min(420.0f, w - 24.0f);
        const float dialog_h = Clamp(dialog_desired.h, 120.0f, std::max(120.0f, content_h - 16.0f));
        active_dialog_->Arrange({(w - dialog_w) * 0.5f,
                                 chrome + (content_h - dialog_h) * 0.5f, dialog_w, dialog_h});
    }
    if (active_busy_) {
        const Size busy_desired = active_busy_->Measure({w, content_h}, theme_);
        const float busy_w = std::min(320.0f, w - 24.0f);
        const float busy_h = Clamp(busy_desired.h, 80.0f, std::max(80.0f, content_h - 16.0f));
        active_busy_->Arrange({(w - busy_w) * 0.5f, chrome + (content_h - busy_h) * 0.5f, busy_w,
                               busy_h});
    }
    LayoutFlyout();
    SyncImeCaret();
}

void WindowImpl::DrawTree(Control* control) {
    const Rect clip = paint_clip_.IsEmpty() ? Rect{-1.0e6f, -1.0e6f, 2.0e6f, 2.0e6f}
                                            : paint_clip_;
    DrawControlTree(painter_, theme_, control, clip);
}

void WindowImpl::Paint() {
    if (painting_) return;
    if (!hwnd_ || client_w_ <= 0 || client_h_ <= 0) return;
    if (renderer_.NeedsRecovery()) {
        backdrop_cache_.reset();
        backdrop_cache_dirty_ = true;
        painter_.InvalidateAcrylic();
        if (!renderer_.Recover()) return;
        layout_dirty_ = true;
        dirty_full_ = true;
    }
    if (!renderer_.Ready() && !renderer_.Recover()) return;
    if (client_w_ != renderer_.Width() || client_h_ != renderer_.Height()) dirty_full_ = true;
    renderer_.Resize(client_w_, client_h_);
    if (renderer_.NeedsRecovery()) {
        backdrop_cache_.reset();
        backdrop_cache_dirty_ = true;
        painter_.InvalidateAcrylic();
        if (!renderer_.Recover()) return;
        layout_dirty_ = true;
        dirty_full_ = true;
        renderer_.Resize(client_w_, client_h_);
        if (renderer_.NeedsRecovery() || !renderer_.Ready()) return;
    }
    painting_ = true;
    LARGE_INTEGER t_frame{};
    QueryPerformanceCounter(&t_frame);
    bool more = false;
    if (animating_) more = TickAnimations();
    if (more) {
        for (Control* current : anim_targets_) {
            if (current) current->Invalidate();
        }
    }
    if (layout_dirty_) {
        Layout();
        dirty_full_ = true;
    }
    const Rect client{0.0f, 0.0f, client_w_ / scale_, client_h_ / scale_};
    if (OverlayWantsAcrylic()) dirty_full_ = true;
    // 浮层在控件树之后画。局部重绘若不把它们并进脏区并保持裁剪，半透明
    // DrawGlow 会叠在 retain 上：鼠标一动 TeachingTip 光晕就越来越亮。
    if (!dirty_full_ && dirty_count_ > 0) {
        auto add_overlay = [&](Control* c) {
            if (c && c->Visible()) AddDirtyRect(c->DirtyBounds());
        };
        add_overlay(active_drawer_);
        add_overlay(active_flyout_);
        add_overlay(active_dialog_);
        add_overlay(active_busy_);
    }
    const bool full = dirty_full_ || dirty_count_ <= 0;
    RECT dirty_px[kMaxDirtyRects]{};
    UINT dirty_n = 0;
    if (full) {
        paint_clip_ = client;
    } else {
        paint_clip_ = dirty_rects_[0];
        for (int i = 0; i < dirty_count_; ++i) {
            paint_clip_ = UnionRect(paint_clip_, dirty_rects_[i]);
            dirty_px[dirty_n++] = DirtyPixelRect(dirty_rects_[i]);
        }
        // 裁剪矩形对齐到与 Present1 相同的像素格，避免 DIP 边落在半像素上。
        {
            const RECT clip_px = DirtyPixelRect(paint_clip_);
            const float s = scale_ > 0.0f ? scale_ : 1.0f;
            paint_clip_ = {static_cast<float>(clip_px.left) / s,
                           static_cast<float>(clip_px.top) / s,
                           static_cast<float>(clip_px.right - clip_px.left) / s,
                           static_cast<float>(clip_px.bottom - clip_px.top) / s};
        }
        // 标题栏 HUD 不进 paint_clip_（并进去会把中间整列拉脏）。只多拷一条到后缓冲。
        if (frame_ == Frame::Client && title_bar_ && dirty_n < kMaxDirtyRects) {
            const RECT cap = DirtyPixelRect(title_bar_->DirtyBounds());
            if (cap.right > cap.left && cap.bottom > cap.top) dirty_px[dirty_n++] = cap;
        }
    }
    dirty_full_ = false;
    dirty_count_ = 0;

    ID2D1DeviceContext2* dc = renderer_.BeginDraw();
    if (!dc) {
        painting_ = false;
        Invalidate();
        return;
    }
    painter_.BeginFrame(dc, &UiText(), scale_);
    painter_.SetLumaText(renderer_.Luma());
    painter_.SetBackdrop(theme_.bg);
    LARGE_INTEGER t_draw{};
    QueryPerformanceCounter(&t_draw);
    if (full) {
        dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    } else {
        painter_.PushClip(paint_clip_, false);
        dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        painter_.PopClip();
    }
    const bool acrylic = OverlayWantsAcrylic();
    const bool freeze = acrylic && painter_.HasAcrylic();
    const bool clip_partial = !full && !freeze;
    if (!freeze) {
        if (clip_partial) painter_.PushClip(paint_clip_, false);
        BlitBackdrop(dc, client);
        DrawTree(root_.get());
        if (acrylic) painter_.CaptureAcrylic();
    }
    if (acrylic) {
        constexpr float kSigma = 16.0f;
        const float t = AcrylicAmount();
        painter_.DrawAcrylic(client, kSigma * t, AcrylicDim() * t);
    }
    if (active_drawer_) DrawTree(active_drawer_);
    if (active_flyout_) DrawTree(active_flyout_);
    if (active_dialog_) DrawTree(active_dialog_);
    if (active_busy_) DrawTree(active_busy_);
    DrawToasts(painter_, theme_, client);
    DrawTooltip(painter_, theme_, client);
    if (clip_partial) painter_.PopClip();
    DrawCaption(client);
    painter_.EndFrame();
    const float draw_ms = QpcMs(t_draw, qpc_freq_);
    LARGE_INTEGER t_present{};
    QueryPerformanceCounter(&t_present);
    const bool presented = renderer_.EndDraw(!Renderer::FlyoutOpen() && !in_size_move_,
                                             full ? nullptr : dirty_px, full ? 0 : dirty_n);
    const float present_ms = QpcMs(t_present, qpc_freq_);
    const float frame_ms = QpcMs(t_frame, qpc_freq_);
    if (frame_ == Frame::Client) {
        const bool first_hud = perf_hud_[0] == L'\0';
        UpdatePerfHud(draw_ms, present_ms, frame_ms, full, dirty_n);
        if (first_hud) paint_again_ = true;
    }
    painting_ = false;
    if (!presented) {
        Invalidate();
        return;
    }
    if (more) {
        for (Control* current : anim_targets_) {
            if (current) current->Invalidate();
        }
        if (!frame_cbs_.empty()) Invalidate();
    }
    if (paint_again_) {
        paint_again_ = false;
        RequestPaint();
    }
    if (!more && dirty_count_ == 0 && !dirty_full_) animating_ = false;
}

bool WindowImpl::TickAnimations() {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = static_cast<float>(now.QuadPart - last_tick_.QuadPart) /
               static_cast<float>(frequency.QuadPart);
    last_tick_ = now;
    dt = std::clamp(dt, 0.0f, 0.1f);

    bool more = false;
    size_t keep = 0;
    if (acrylic_tween_.running) {
        if (acrylic_tween_.Tick(dt)) more = true;
    }
    for (size_t i = 0; i < anim_targets_.size(); ++i) {
        Control* current = anim_targets_[i];
        if (!current || !current->visible_) {
            if (current) current->anim_listed_ = false;
            continue;
        }
        if (current->OnAnimate(dt)) {
            more = true;
            anim_targets_[keep++] = current;
        } else {
            current->anim_listed_ = false;
        }
    }
    anim_targets_.resize(keep);
    size_t frame_keep = 0;
    for (size_t i = 0; i < frame_cbs_.size(); ++i) {
        FrameCb cb = std::move(frame_cbs_[i]);
        if (!cb.fn) continue;
        if (cb.fn(dt)) {
            more = true;
            frame_cbs_[frame_keep++] = std::move(cb);
        }
    }
    frame_cbs_.resize(frame_keep);
    return more;
}

void WindowImpl::LayoutNow() {
    UpdateClientSize();
    Layout();
}

void WindowImpl::DispatchMouseMove(Point client_dip, uint32_t buttons) {
    OnMouseMove(static_cast<int>(client_dip.x * scale_ + 0.5f),
                static_cast<int>(client_dip.y * scale_ + 0.5f), buttons);
}

void WindowImpl::DispatchMouseButton(Point client_dip, uint32_t buttons, bool down,
                                     uint32_t changed) {
    OnMouseButton(static_cast<int>(client_dip.x * scale_ + 0.5f),
                  static_cast<int>(client_dip.y * scale_ + 0.5f), buttons, down, changed);
}

bool WindowImpl::DispatchKey(uint32_t vk) { return OnKeyDown(vk); }

} // namespace lumen

// ime_bridge.cpp — WindowImpl IME 组字、候选窗与插入符同步。
#include "window_impl.h"
#include "text_service.h"
#include <algorithm>
#include <imm.h>
#include <string>

namespace lumen {

bool WindowImpl::ImeClientCaret(POINT* caret_px, int* line_h_px, RECT* doc_px) const {
    Point dip{};
    float height_dip = 0.0f;
    if (!focused_ || !focused_->ImeCaret(dip, height_dip)) return false;
    if (caret_px) {
        caret_px->x = static_cast<LONG>(dip.x * scale_ + 0.5f);
        caret_px->y = static_cast<LONG>(dip.y * scale_ + 0.5f);
    }
    if (line_h_px) {
        *line_h_px = std::max(1, static_cast<int>(height_dip * scale_ + 0.5f));
    }
    if (doc_px) {
        const Rect& box = focused_->AbsoluteBounds();
        doc_px->left = static_cast<LONG>(box.x * scale_);
        doc_px->top = static_cast<LONG>(box.y * scale_);
        doc_px->right = static_cast<LONG>(box.Right() * scale_);
        doc_px->bottom = static_cast<LONG>(box.Bottom() * scale_);
    }
    return true;
}

void WindowImpl::HandleImeComposition(LPARAM lparam) {
    if (!hwnd_ || !focused_) return;
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;

    auto take_string = [&](DWORD type) -> std::wstring {
        const LONG bytes = ImmGetCompositionStringW(himc, type, nullptr, 0);
        if (bytes <= 0) return {};
        std::wstring text(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
        ImmGetCompositionStringW(himc, type, text.data(), bytes);
        while (!text.empty() && text.back() == L'\0') text.pop_back();
        return text;
    };

    if (lparam & GCS_RESULTSTR) {
        focused_->OnImeCommit(take_string(GCS_RESULTSTR));
    }
    if (lparam & GCS_COMPSTR) {
        const std::wstring comp = take_string(GCS_COMPSTR);
        LONG cursor = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
        if (cursor < 0) cursor = static_cast<LONG>(comp.size());
        std::string attr;
        if (lparam & GCS_COMPATTR) {
            const LONG n = ImmGetCompositionStringW(himc, GCS_COMPATTR, nullptr, 0);
            if (n > 0) {
                attr.resize(static_cast<size_t>(n));
                ImmGetCompositionStringW(himc, GCS_COMPATTR, attr.data(), n);
            }
        }
        focused_->OnImeCompose(comp, static_cast<size_t>(cursor), attr);
    }
    ImmReleaseContext(hwnd_, himc);
    SyncImeCaret();
}

void WindowImpl::SyncImeCaret() {
    if (!hwnd_ || ime_syncing_) return;
    POINT caret{};
    int line_h = 0;
    RECT doc{};
    if (!ImeClientCaret(&caret, &line_h, &doc)) return;
    // ImmSet* 会再投递 WM_IME_NOTIFY / SETCONTEXT，必须挡住重入，否则栈溢出直接退出。
    // CreateCaret/ShowCaret 不能用：WS_EX_NOREDIRECTIONBITMAP 没有 GDI 表面。
    ime_syncing_ = true;
    if (HIMC himc = ImmGetContext(hwnd_)) {
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = caret;
        ImmSetCompositionWindow(himc, &composition);
        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = {caret.x, caret.y + line_h};
        candidate.rcArea = doc;
        ImmSetCandidateWindow(himc, &candidate);
        ImmReleaseContext(hwnd_, himc);
    }
    ime_syncing_ = false;
}

bool WindowImpl::OnImeRequest(WPARAM wparam, LPARAM lparam, LRESULT* result) {
    if (!lparam || !result) return false;
    POINT caret{};
    int line_h = 0;
    RECT doc{};
    if (!ImeClientCaret(&caret, &line_h, &doc)) return false;
    switch (wparam) {
    case IMR_QUERYCHARPOSITION: {
        auto* info = reinterpret_cast<IMECHARPOSITION*>(lparam);
        POINT screen = caret;
        ClientToScreen(hwnd_, &screen);
        info->pt = screen;
        info->cLineHeight = static_cast<UINT>(line_h);
        RECT screen_doc = doc;
        POINT tl{doc.left, doc.top};
        POINT br{doc.right, doc.bottom};
        ClientToScreen(hwnd_, &tl);
        ClientToScreen(hwnd_, &br);
        screen_doc = {tl.x, tl.y, br.x, br.y};
        info->rcDocument = screen_doc;
        *result = TRUE;
        return true;
    }
    case IMR_COMPOSITIONWINDOW: {
        auto* form = reinterpret_cast<COMPOSITIONFORM*>(lparam);
        form->dwStyle = CFS_POINT;
        form->ptCurrentPos = caret;
        *result = TRUE;
        return true;
    }
    case IMR_CANDIDATEWINDOW: {
        auto* form = reinterpret_cast<CANDIDATEFORM*>(lparam);
        form->dwStyle = CFS_EXCLUDE;
        form->ptCurrentPos = {caret.x, caret.y + line_h};
        form->rcArea = doc;
        *result = TRUE;
        return true;
    }
    default:
        return false;
    }
}

} // namespace lumen

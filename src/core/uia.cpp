// uia.cpp — 窗口级 UI Automation 提供程序。COM 类型不出公共头；模式派发走 Control 虚函数。
#include "window_impl.h"
#include "lumen/Panel.h"
#include "lumen/TitleBar.h"
#include <UIAutomation.h>
#include <atomic>
#include <oleauto.h>
#include <unordered_map>
#include <vector>

namespace lumen {
namespace {

LONG ControlTypeId(AutomationControlType type) noexcept {
    switch (type) {
    case AutomationControlType::Pane: return UIA_PaneControlTypeId;
    case AutomationControlType::Group: return UIA_GroupControlTypeId;
    case AutomationControlType::Button: return UIA_ButtonControlTypeId;
    case AutomationControlType::CheckBox: return UIA_CheckBoxControlTypeId;
    case AutomationControlType::RadioButton: return UIA_RadioButtonControlTypeId;
    case AutomationControlType::Edit: return UIA_EditControlTypeId;
    case AutomationControlType::Slider: return UIA_SliderControlTypeId;
    case AutomationControlType::ProgressBar: return UIA_ProgressBarControlTypeId;
    case AutomationControlType::List: return UIA_ListControlTypeId;
    case AutomationControlType::DataGrid: return UIA_DataGridControlTypeId;
    case AutomationControlType::ComboBox: return UIA_ComboBoxControlTypeId;
    case AutomationControlType::Tab: return UIA_TabControlTypeId;
    case AutomationControlType::Tree: return UIA_TreeControlTypeId;
    case AutomationControlType::Text: return UIA_TextControlTypeId;
    case AutomationControlType::Hyperlink: return UIA_HyperlinkControlTypeId;
    case AutomationControlType::Image: return UIA_ImageControlTypeId;
    case AutomationControlType::Header: return UIA_HeaderControlTypeId;
    case AutomationControlType::StatusBar: return UIA_StatusBarControlTypeId;
    case AutomationControlType::ToolTip: return UIA_ToolTipControlTypeId;
    case AutomationControlType::Separator: return UIA_SeparatorControlTypeId;
    case AutomationControlType::SplitButton: return UIA_SplitButtonControlTypeId;
    case AutomationControlType::MenuBar: return UIA_MenuBarControlTypeId;
    case AutomationControlType::Window: return UIA_WindowControlTypeId;
    default: return UIA_CustomControlTypeId;
    }
}

void SetBstr(VARIANT* v, const std::wstring& text) {
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocStringLen(text.c_str(), static_cast<UINT>(text.size()));
}

void SetBool(VARIANT* v, bool value) {
    v->vt = VT_BOOL;
    v->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

void SetI4(VARIANT* v, LONG value) {
    v->vt = VT_I4;
    v->lVal = value;
}

SAFEARRAY* RuntimeId(uintptr_t a, uintptr_t b, int extra = -1) {
    const ULONG n = extra >= 0 ? 4u : 3u;
    SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, n);
    if (!sa) return nullptr;
    LONG* data = nullptr;
    if (FAILED(SafeArrayAccessData(sa, reinterpret_cast<void**>(&data))) || !data) {
        SafeArrayDestroy(sa);
        return nullptr;
    }
    data[0] = UiaAppendRuntimeId;
    data[1] = static_cast<LONG>(a >> 32);
    data[2] = static_cast<LONG>(a);
    if (extra >= 0) data[3] = extra + 1;
    (void)b;
    SafeArrayUnaccessData(sa);
    return sa;
}

} // namespace

struct UiaState {
    WindowImpl* impl = nullptr;
    struct UiaNode* root = nullptr;
    std::unordered_map<const Control*, struct UiaNode*> nodes;
};

struct UiaNode final : IRawElementProviderSimple,
                       IRawElementProviderFragment,
                       IRawElementProviderFragmentRoot,
                       IInvokeProvider,
                       IToggleProvider,
                       IValueProvider,
                       IRangeValueProvider,
                       IExpandCollapseProvider,
                       ISelectionProvider,
                       ISelectionItemProvider {
    WindowImpl* impl = nullptr;
    Control* control = nullptr;
    int item_index = -1;
    std::atomic<ULONG> refs{1};

    bool IsRoot() const noexcept { return control == nullptr && item_index < 0; }
    bool IsGhost() const noexcept { return item_index >= 0; }
    uint32_t Patterns() const noexcept {
        return control ? control->AutomationPatterns() : 0;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return refs.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
        // 树节点由 UiaState 持有；幽灵项不进 map，归零即释放。
        if (n == 0 && IsGhost()) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *ppv = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == IID_IRawElementProviderFragment) {
            *ppv = static_cast<IRawElementProviderFragment*>(this);
        } else if (riid == IID_IRawElementProviderFragmentRoot && IsRoot()) {
            *ppv = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (riid == IID_IInvokeProvider && (Patterns() & kPatternInvoke) && !IsGhost()) {
            *ppv = static_cast<IInvokeProvider*>(this);
        } else if (riid == IID_IToggleProvider && (Patterns() & kPatternToggle) && !IsGhost()) {
            *ppv = static_cast<IToggleProvider*>(this);
        } else if (riid == IID_IValueProvider && (Patterns() & kPatternValue) && !IsGhost()) {
            *ppv = static_cast<IValueProvider*>(this);
        } else if (riid == IID_IRangeValueProvider && (Patterns() & kPatternRange) && !IsGhost()) {
            *ppv = static_cast<IRangeValueProvider*>(this);
        } else if (riid == IID_IExpandCollapseProvider && (Patterns() & kPatternExpand) && !IsGhost()) {
            *ppv = static_cast<IExpandCollapseProvider*>(this);
        } else if (riid == IID_ISelectionProvider && (Patterns() & kPatternSelection) && !IsGhost()) {
            *ppv = static_cast<ISelectionProvider*>(this);
        } else if (riid == IID_ISelectionItemProvider &&
                   (IsGhost() || (Patterns() & kPatternSelectionItem))) {
            *ppv = static_cast<ISelectionItemProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* ret) override {
        if (!ret) return E_POINTER;
        *ret = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                            ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** ret) override {
        if (!ret) return E_POINTER;
        *ret = nullptr;
        IUnknown* p = nullptr;
        if (IsGhost()) {
            if (id == UIA_SelectionItemPatternId) p = static_cast<ISelectionItemProvider*>(this);
        } else {
            const uint32_t pat = Patterns();
            if (id == UIA_InvokePatternId && (pat & kPatternInvoke)) {
                p = static_cast<IInvokeProvider*>(this);
            } else if (id == UIA_TogglePatternId && (pat & kPatternToggle)) {
                p = static_cast<IToggleProvider*>(this);
            } else if (id == UIA_ValuePatternId && (pat & kPatternValue)) {
                p = static_cast<IValueProvider*>(this);
            } else if (id == UIA_RangeValuePatternId && (pat & kPatternRange)) {
                p = static_cast<IRangeValueProvider*>(this);
            } else if (id == UIA_ExpandCollapsePatternId && (pat & kPatternExpand)) {
                p = static_cast<IExpandCollapseProvider*>(this);
            } else if (id == UIA_SelectionPatternId && (pat & kPatternSelection)) {
                p = static_cast<ISelectionProvider*>(this);
            } else if (id == UIA_SelectionItemPatternId && (pat & kPatternSelectionItem)) {
                p = static_cast<ISelectionItemProvider*>(this);
            }
        }
        if (p) {
            p->AddRef();
            *ret = p;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* ret) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** ret) override {
        if (!ret) return E_POINTER;
        *ret = nullptr;
        if (!IsRoot() || !impl || !impl->hwnd_) return S_OK;
        return UiaHostProviderFromHwnd(impl->hwnd_, ret);
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** ret) override {
        if (!ret) return E_POINTER;
        const uintptr_t key = IsRoot() ? 1 : reinterpret_cast<uintptr_t>(control);
        *ret = RuntimeId(key, 0, IsGhost() ? item_index : -1);
        return *ret ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* ret) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** ret) override {
        if (!ret) return E_POINTER;
        *ret = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        if (IsGhost() || !impl) return S_OK;
        if (control && impl->UiaFocusable(control)) impl->SetFocusControl(control);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** ret) override;

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** ret) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** ret) override;

    HRESULT STDMETHODCALLTYPE Invoke() override {
        if (!control || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!control->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return control->AutomationInvoke() ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override {
        if (!control || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!control->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return control->AutomationToggle() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* ret) override {
        if (!ret) return E_POINTER;
        const int s = control ? control->AutomationToggleState() : -1;
        *ret = s == 1 ? ToggleState_On : (s == 2 ? ToggleState_Indeterminate : ToggleState_Off);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        if (!control || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!control->Enabled() || control->AutomationIsReadOnly()) return UIA_E_ELEMENTNOTENABLED;
        return control->AutomationSetValue(value ? value : L"") ? S_OK : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* ret) override {
        if (!ret) return E_POINTER;
        const std::wstring text = control ? control->AutomationValue() : std::wstring{};
        *ret = SysAllocStringLen(text.c_str(), static_cast<UINT>(text.size()));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* ret) override {
        if (!ret) return E_POINTER;
        *ret = (!control || control->AutomationIsReadOnly()) ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        if (!control || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!control->Enabled() || control->AutomationIsReadOnly()) return UIA_E_ELEMENTNOTENABLED;
        return control->AutomationSetRange(value) ? S_OK : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* ret) override {
        if (!ret) return E_POINTER;
        *ret = control ? control->AutomationRangeValue() : 0.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* ret) override {
        if (!ret) return E_POINTER;
        *ret = control ? control->AutomationRangeMax() : 0.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* ret) override {
        if (!ret) return E_POINTER;
        *ret = control ? control->AutomationRangeMin() : 0.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* ret) override {
        if (!ret) return E_POINTER;
        *ret = control ? control->AutomationRangeLarge() : 10.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* ret) override {
        if (!ret) return E_POINTER;
        *ret = control ? control->AutomationRangeSmall() : 1.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Expand() override {
        if (!control || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!control->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return control->AutomationExpand() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE Collapse() override {
        if (!control || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!control->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return control->AutomationCollapse() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* ret) override {
        if (!ret) return E_POINTER;
        const int s = control ? control->AutomationExpandState() : -1;
        *ret = s == 1 ? ExpandCollapseState_Expanded
                      : (s == 0 ? ExpandCollapseState_Collapsed : ExpandCollapseState_LeafNode);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** ret) override;
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* ret) override {
        if (!ret) return E_POINTER;
        *ret = (control && control->AutomationCanSelectMultiple()) ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* ret) override {
        if (!ret) return E_POINTER;
        *ret = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* ret) override;
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** ret) override;
    static UiaState* StateOf(WindowImpl* w);
    static UiaNode* RootOf(WindowImpl* w);
    static UiaNode* NodeFor(WindowImpl* w, Control* c);
    static UiaNode* GhostOf(WindowImpl* w, Control* host, int index);
    static void CollectRootChildren(WindowImpl* w, std::vector<Control*>& out);
    static void CollectChildren(WindowImpl* w, Control* c, std::vector<Control*>& out);
};

UiaState* UiaNode::StateOf(WindowImpl* w) {
    if (!w->uia_state_) {
        auto* state = new UiaState;
        state->impl = w;
        w->uia_state_ = state;
    }
    return static_cast<UiaState*>(w->uia_state_);
}

UiaNode* UiaNode::RootOf(WindowImpl* w) {
    UiaState* state = StateOf(w);
    if (!state->root) {
        state->root = new UiaNode;
        state->root->impl = w;
    }
    return state->root;
}

UiaNode* UiaNode::NodeFor(WindowImpl* w, Control* c) {
    if (!c) return RootOf(w);
    UiaState* state = StateOf(w);
    UiaNode*& slot = state->nodes[c];
    if (!slot) {
        slot = new UiaNode;
        slot->impl = w;
        slot->control = c;
    }
    return slot;
}

UiaNode* UiaNode::GhostOf(WindowImpl* w, Control* host, int index) {
    auto* node = new UiaNode;
    node->impl = w;
    node->control = host;
    node->item_index = index;
    return node;
}

void UiaNode::CollectRootChildren(WindowImpl* w, std::vector<Control*>& out) {
    if (TitleBar* bar = w->title_bar_.get()) {
        if (bar->Visible()) out.push_back(bar);
    }
    if (w->root_ && w->root_->Visible()) out.push_back(w->root_.get());
    if (w->active_dialog_ && w->active_dialog_->Visible()) out.push_back(w->active_dialog_);
    if (w->active_drawer_ && w->active_drawer_->Visible()) out.push_back(w->active_drawer_);
    if (w->active_busy_ && w->active_busy_->Visible()) out.push_back(w->active_busy_);
    if (w->active_flyout_ && w->active_flyout_->Visible()) out.push_back(w->active_flyout_);
}

void UiaNode::CollectChildren(WindowImpl* w, Control* c, std::vector<Control*>& out) {
    if (!c) {
        CollectRootChildren(w, out);
        return;
    }
    if (const Panel* panel = c->AsPanel()) {
        for (size_t i = 0; i < panel->ChildCount(); ++i) {
            Control& child = panel->Child(i);
            if (child.Visible()) out.push_back(&child);
        }
    }
}

HRESULT UiaNode::GetPropertyValue(PROPERTYID id, VARIANT* ret) {
    if (!ret) return E_POINTER;
    VariantInit(ret);
    const bool enabled = !control || control->Enabled();
    if (id == UIA_NamePropertyId) {
        std::wstring name;
        if (IsGhost() && control) name = control->AutomationItemName(item_index);
        else if (control) name = control->AutomationName();
        else if (impl) name = impl->title_;
        SetBstr(ret, name);
        return S_OK;
    }
    if (id == UIA_ControlTypePropertyId) {
        AutomationControlType type = AutomationControlType::Window;
        if (IsGhost()) type = AutomationControlType::List;
        else if (control) type = control->AutomationType();
        SetI4(ret, ControlTypeId(type));
        return S_OK;
    }
    if (id == UIA_IsEnabledPropertyId) {
        SetBool(ret, enabled);
        return S_OK;
    }
    if (id == UIA_IsKeyboardFocusablePropertyId) {
        SetBool(ret, !IsGhost() && impl && impl->UiaFocusable(control));
        return S_OK;
    }
    if (id == UIA_HasKeyboardFocusPropertyId) {
        SetBool(ret, !IsGhost() && control && impl && impl->focused_ == control);
        return S_OK;
    }
    if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
        SetBool(ret, true);
        return S_OK;
    }
    if (id == UIA_IsPasswordPropertyId) {
        SetBool(ret, control && !IsGhost() && control->AutomationIsPassword());
        return S_OK;
    }
    if (id == UIA_LiveSettingPropertyId) {
        SetI4(ret, control && !IsGhost() ? control->AutomationLiveSetting() : 0);
        return S_OK;
    }
    if (id == UIA_FrameworkIdPropertyId) {
        SetBstr(ret, L"LUMEN");
        return S_OK;
    }
    if (id == UIA_NativeWindowHandlePropertyId && IsRoot() && impl && impl->hwnd_) {
        SetI4(ret, static_cast<LONG>(reinterpret_cast<uintptr_t>(impl->hwnd_)));
        return S_OK;
    }
    return S_OK;
}

HRESULT UiaNode::get_BoundingRectangle(UiaRect* ret) {
    if (!ret) return E_POINTER;
    *ret = {};
    if (!impl || !impl->hwnd_) return S_OK;
    RECT client{};
    GetClientRect(impl->hwnd_, &client);
    POINT origin{0, 0};
    ClientToScreen(impl->hwnd_, &origin);
    const float scale = impl->scale_ > 0.0f ? impl->scale_ : 1.0f;
    if (IsRoot() || IsGhost() || !control) {
        ret->left = origin.x;
        ret->top = origin.y;
        ret->width = client.right;
        ret->height = client.bottom;
        return S_OK;
    }
    const Rect& r = control->AbsoluteBounds();
    POINT tl{static_cast<LONG>(r.x * scale), static_cast<LONG>(r.y * scale)};
    ClientToScreen(impl->hwnd_, &tl);
    ret->left = tl.x;
    ret->top = tl.y;
    ret->width = r.w * scale;
    ret->height = r.h * scale;
    return S_OK;
}

HRESULT UiaNode::get_FragmentRoot(IRawElementProviderFragmentRoot** ret) {
    if (!ret) return E_POINTER;
    if (!impl) {
        *ret = nullptr;
        return S_OK;
    }
    UiaNode* root = RootOf(impl);
    root->AddRef();
    *ret = static_cast<IRawElementProviderFragmentRoot*>(root);
    return S_OK;
}

HRESULT UiaNode::Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!impl) return S_OK;
    if (IsGhost()) {
        if (dir == NavigateDirection_Parent && control) {
            UiaNode* parent = NodeFor(impl, control);
            parent->AddRef();
            *ret = parent;
        }
        return S_OK;
    }
    std::vector<Control*> siblings;
    Control* parent = impl->UiaParentOf(control);
    if (control) CollectChildren(impl, parent, siblings);
    else CollectRootChildren(impl, siblings);

    auto wrap = [&](Control* c) {
        if (!c) return;
        UiaNode* node = NodeFor(impl, c);
        node->AddRef();
        *ret = node;
    };

    if (dir == NavigateDirection_Parent) {
        if (!control) return S_OK;
        if (parent) wrap(parent);
        else {
            UiaNode* root = RootOf(impl);
            root->AddRef();
            *ret = root;
        }
        return S_OK;
    }
    std::vector<Control*> kids;
    CollectChildren(impl, control, kids);
    if (dir == NavigateDirection_FirstChild) {
        if (!kids.empty()) wrap(kids.front());
        return S_OK;
    }
    if (dir == NavigateDirection_LastChild) {
        if (!kids.empty()) wrap(kids.back());
        return S_OK;
    }
    if (!control) return S_OK;
    ptrdiff_t at = -1;
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == control) {
            at = static_cast<ptrdiff_t>(i);
            break;
        }
    }
    if (at < 0) return S_OK;
    if (dir == NavigateDirection_NextSibling && static_cast<size_t>(at + 1) < siblings.size()) {
        wrap(siblings[static_cast<size_t>(at + 1)]);
    } else if (dir == NavigateDirection_PreviousSibling && at > 0) {
        wrap(siblings[static_cast<size_t>(at - 1)]);
    }
    return S_OK;
}

HRESULT UiaNode::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!impl || !impl->hwnd_) return S_OK;
    POINT screen{static_cast<LONG>(x), static_cast<LONG>(y)};
    POINT client = screen;
    ScreenToClient(impl->hwnd_, &client);
    const float scale = impl->scale_ > 0.0f ? impl->scale_ : 1.0f;
    Control* hit = impl->HitTest({static_cast<float>(client.x) / scale,
                                  static_cast<float>(client.y) / scale});
    UiaNode* node = hit ? NodeFor(impl, hit) : RootOf(impl);
    node->AddRef();
    *ret = node;
    return S_OK;
}

HRESULT UiaNode::GetFocus(IRawElementProviderFragment** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!impl) return S_OK;
    UiaNode* node = impl->focused_ ? NodeFor(impl, impl->focused_) : RootOf(impl);
    node->AddRef();
    *ret = node;
    return S_OK;
}

HRESULT UiaNode::GetSelection(SAFEARRAY** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!control || IsGhost()) return S_OK;
    const int index = control->AutomationSelectedIndex();
    if (index < 0) {
        *ret = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return *ret ? S_OK : E_OUTOFMEMORY;
    }
    UiaNode* item = GhostOf(impl, control, index);
    SAFEARRAY* sa = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (!sa) {
        item->Release();
        return E_OUTOFMEMORY;
    }
    LONG i = 0;
    IUnknown* unk = static_cast<IUnknown*>(static_cast<IRawElementProviderSimple*>(item));
    const HRESULT hr = SafeArrayPutElement(sa, &i, unk);
    item->Release();
    if (FAILED(hr)) {
        SafeArrayDestroy(sa);
        return hr;
    }
    *ret = sa;
    return S_OK;
}

HRESULT UiaNode::Select() {
    if (!control) return UIA_E_INVALIDOPERATION;
    if (!control->Enabled()) return UIA_E_ELEMENTNOTENABLED;
    if (IsGhost()) {
        return control->AutomationSelectIndex(item_index) ? S_OK : E_INVALIDARG;
    }
    if (Patterns() & kPatternSelectionItem) {
        if (control->AutomationToggleState() == 1) return S_OK;
        return control->AutomationToggle() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    return UIA_E_INVALIDOPERATION;
}

HRESULT UiaNode::get_IsSelected(BOOL* ret) {
    if (!ret) return E_POINTER;
    *ret = FALSE;
    if (!control) return S_OK;
    if (IsGhost()) {
        *ret = control->AutomationSelectedIndex() == item_index ? TRUE : FALSE;
    } else {
        *ret = control->AutomationToggleState() == 1 ? TRUE : FALSE;
    }
    return S_OK;
}

HRESULT UiaNode::get_SelectionContainer(IRawElementProviderSimple** ret) {
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!impl || !control) return S_OK;
    Control* host = IsGhost() ? control : impl->UiaParentOf(control);
    UiaNode* node = host ? NodeFor(impl, host) : RootOf(impl);
    node->AddRef();
    *ret = node;
    return S_OK;
}

LRESULT WindowImpl::UiaGetObject(WPARAM wparam, LPARAM lparam) {
    if (!hwnd_ || static_cast<LONG>(lparam) != static_cast<LONG>(UiaRootObjectId)) {
        return DefWindowProcW(hwnd_, WM_GETOBJECT, wparam, lparam);
    }
    UiaNode* root = UiaNode::RootOf(this);
    return UiaReturnRawElementProvider(hwnd_, wparam, lparam,
                                       static_cast<IRawElementProviderSimple*>(root));
}

void WindowImpl::UiaShutdown() {
    auto* state = static_cast<UiaState*>(uia_state_);
    if (!state) return;
    for (auto& pair : state->nodes) {
        if (pair.second) {
            UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(pair.second));
            delete pair.second;
        }
    }
    state->nodes.clear();
    if (state->root) {
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(state->root));
        delete state->root;
        state->root = nullptr;
    }
    delete state;
    uia_state_ = nullptr;
}

void WindowImpl::UiaOnFocus() {
    if (!uia_state_ || !hwnd_) return;
    UiaNode* node = focused_ ? UiaNode::NodeFor(this, focused_) : UiaNode::RootOf(this);
    UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(node),
                            UIA_AutomationFocusChangedEventId);
}

void WindowImpl::UiaForget(const Control* control) {
    auto* state = static_cast<UiaState*>(uia_state_);
    if (!state || !control) return;
    const auto it = state->nodes.find(control);
    if (it == state->nodes.end()) return;
    UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(it->second));
    delete it->second;
    state->nodes.erase(it);
}

} // namespace lumen

// uia.cpp — 窗口级 UI Automation 提供程序。COM 类型不出公共头；模式派发走 Control 虚函数。
#include "window_impl.h"
#include "native_callback.h"
#include "log.h"
#include <memory>
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

SAFEARRAY* RuntimeId(uintptr_t a, uintptr_t b, int extra = -1, int column = -1) {
    const ULONG n = column >= 0 ? 5u : extra >= 0 ? 4u : 3u;
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
    if (column >= 0) data[4] = column + 1;
    (void)b;
    SafeArrayUnaccessData(sa);
    return sa;
}

} // namespace

struct UiaNode;
struct UiaLink {
    WindowImpl* impl = nullptr;
    Control* control = nullptr;
    UiaNode* head = nullptr; // 非拥有链，只用于断开包含 Ghost 在内的全部 provider。
    IRawElementProviderFragmentRoot* root_provider = nullptr;
    UiaLink(WindowImpl* window, Control* target,
            IRawElementProviderFragmentRoot* identity = nullptr)
        : impl(window), control(target), root_provider(identity) {
        if (root_provider) root_provider->AddRef();
    }
    ~UiaLink() { if (root_provider) root_provider->Release(); }
    UiaLink(const UiaLink&) = delete;
    UiaLink& operator=(const UiaLink&) = delete;
};
namespace {
std::atomic<unsigned> g_live_providers{0};
UiaNode* g_pending_disconnect = nullptr;
}

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
                       ISelectionItemProvider,
                       IGridProvider,
                       IGridItemProvider {
    std::shared_ptr<UiaLink> link;
    UiaNode* link_next = nullptr;
    UiaNode* link_prev = nullptr;
    UiaNode* pending_next = nullptr;
    bool pending = false;
    bool root = false;
    uint32_t patterns = 0;
    uintptr_t runtime_key = 0;
    IRawElementProviderSimple* host_provider = nullptr;
    HRESULT host_status = S_OK;
    int item_index = -1;
    int item_column = -1;
    UiaNode(std::shared_ptr<UiaLink> value, bool is_root, int index = -1)
        : link(std::move(value)), root(is_root), item_index(index) {
        patterns = link->control ? link->control->AutomationPatterns() : 0;
        runtime_key = root ? 1 : reinterpret_cast<uintptr_t>(link->control);
        link_next = link->head;
        if (link_next) link_next->link_prev = this;
        link->head = this;
        g_live_providers.fetch_add(1);
    }
    ~UiaNode() {
        if (host_provider) host_provider->Release();
        if (link_prev) link_prev->link_next = link_next;
        else link->head = link_next;
        if (link_next) link_next->link_prev = link_prev;
        g_live_providers.fetch_sub(1);
    }
    WindowImpl* Impl() const noexcept { return link->impl; }
    Control* Target() const noexcept { return link->control; }
    bool Available() const noexcept {
        return Impl() && (root || Target()) && (!IsGhost() ||
            (item_index < Target()->AutomationItemCount() &&
             (!IsCell() || item_column < Target()->AutomationColumnCount())));
    }
    std::atomic<ULONG> refs{1};

    bool IsRoot() const noexcept { return root; }
    bool IsGhost() const noexcept { return item_index >= 0; }
    bool IsCell() const noexcept { return item_column >= 0; }
    uint32_t Patterns() const noexcept {
        return patterns;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { NativeCallbackScope callback; return refs.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG STDMETHODCALLTYPE Release() override { NativeCallbackScope callback;
        const ULONG n = refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
        // state 与外部 COM 客户端各自释放自己的引用。
        if (n == 0) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { NativeCallbackScope callback;
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
        } else if (riid == IID_IValueProvider && (IsCell() || ((Patterns() & kPatternValue) && !IsGhost()))) {
            *ppv = static_cast<IValueProvider*>(this);
        } else if (riid == IID_IRangeValueProvider && (Patterns() & kPatternRange) && !IsGhost()) {
            *ppv = static_cast<IRangeValueProvider*>(this);
        } else if (riid == IID_IExpandCollapseProvider && (Patterns() & kPatternExpand) && !IsGhost()) {
            *ppv = static_cast<IExpandCollapseProvider*>(this);
        } else if (riid == IID_ISelectionProvider && (Patterns() & kPatternSelection) && !IsGhost()) {
            *ppv = static_cast<ISelectionProvider*>(this);
        } else if (riid == IID_IGridProvider && (Patterns() & kPatternGrid) && !IsGhost()) {
            *ppv = static_cast<IGridProvider*>(this);
        } else if (riid == IID_IGridItemProvider && IsCell()) {
            *ppv = static_cast<IGridItemProvider*>(this);
        } else if (riid == IID_ISelectionItemProvider &&
                   (IsGhost() || (Patterns() & kPatternSelectionItem))) {
            *ppv = static_cast<ISelectionItemProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                            ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = nullptr;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        IUnknown* p = nullptr;
        if (IsGhost()) {
            if (id == UIA_SelectionItemPatternId) p = static_cast<ISelectionItemProvider*>(this);
            else if (IsCell() && id == UIA_GridItemPatternId) p = static_cast<IGridItemProvider*>(this);
            else if (IsCell() && id == UIA_ValuePatternId) p = static_cast<IValueProvider*>(this);
        } else {
            const uint32_t pat = Patterns();
            if (id == UIA_GridPatternId && (pat & kPatternGrid)) {
                p = static_cast<IGridProvider*>(this);
            } else if (id == UIA_InvokePatternId && (pat & kPatternInvoke)) {
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
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = host_provider;
        if (*ret) (*ret)->AddRef();
        return host_status;
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        const uintptr_t key = runtime_key;
        *ret = RuntimeId(key, 0, IsGhost() ? item_index : -1, item_column);
        return *ret ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* ret) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (IsGhost() || !Impl()) return S_OK;
        if (Target() && Impl()->UiaFocusable(Target())) Impl()->SetFocusControl(Target());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** ret) override;

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** ret) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** ret) override;

    HRESULT STDMETHODCALLTYPE Invoke() override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!Target() || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!Target()->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return Target()->AutomationInvoke() ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!Target() || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!Target()->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return Target()->AutomationToggle() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        const int s = Target() ? Target()->AutomationToggleState() : -1;
        *ret = s == 1 ? ToggleState_On : (s == 2 ? ToggleState_Indeterminate : ToggleState_Off);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (IsCell()) {
            if (!Target()->Enabled()) return UIA_E_ELEMENTNOTENABLED;
            if (Target()->AutomationCellReadOnly(item_index, item_column)) return UIA_E_INVALIDOPERATION;
            return Target()->AutomationSetCellValue(item_index, item_column, value ? value : L"") ? S_OK : E_INVALIDARG;
        }
        if (!Target() || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!Target()->Enabled() || Target()->AutomationIsReadOnly()) return UIA_E_ELEMENTNOTENABLED;
        return Target()->AutomationSetValue(value ? value : L"") ? S_OK : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        const std::wstring text = IsCell() ? Target()->AutomationCellValue(item_index, item_column) :
            Target() ? Target()->AutomationValue() : std::wstring{};
        *ret = SysAllocStringLen(text.c_str(), static_cast<UINT>(text.size()));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = (IsCell() ? Target()->AutomationCellReadOnly(item_index, item_column) :
            !Target() || Target()->AutomationIsReadOnly()) ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double value) override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!Target() || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!Target()->Enabled() || Target()->AutomationIsReadOnly()) return UIA_E_ELEMENTNOTENABLED;
        return Target()->AutomationSetRange(value) ? S_OK : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = Target() ? Target()->AutomationRangeValue() : 0.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = Target() ? Target()->AutomationRangeMax() : 0.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = Target() ? Target()->AutomationRangeMin() : 0.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = Target() ? Target()->AutomationRangeLarge() : 10.0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = Target() ? Target()->AutomationRangeSmall() : 1.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Expand() override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!Target() || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!Target()->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return Target()->AutomationExpand() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE Collapse() override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!Target() || IsGhost()) return UIA_E_INVALIDOPERATION;
        if (!Target()->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        return Target()->AutomationCollapse() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        const int s = Target() ? Target()->AutomationExpandState() : -1;
        *ret = s == 1 ? ExpandCollapseState_Expanded
                      : (s == 0 ? ExpandCollapseState_Collapsed : ExpandCollapseState_LeafNode);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** ret) override;
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = (Target() && Target()->AutomationCanSelectMultiple()) ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = {};
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE; return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { NativeCallbackScope callback;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE; return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* ret) override;
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** ret) override;
    HRESULT STDMETHODCALLTYPE GetItem(int row, int column, IRawElementProviderSimple** ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = nullptr;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!Target() || IsGhost() || row < 0 || column < 0 ||
            row >= Target()->AutomationItemCount() || column >= Target()->AutomationColumnCount()) return E_INVALIDARG;
        auto* cell = GhostOf(Impl(), Target(), row);
        cell->item_column = column;
        *ret = cell;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RowCount(int* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = 0;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = Target() ? Target()->AutomationItemCount() : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ColumnCount(int* ret) override { NativeCallbackScope callback;
        if (!ret) return E_POINTER;
        *ret = 0;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = Target() ? Target()->AutomationColumnCount() : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Row(int* ret) override { NativeCallbackScope callback; return CellCoordinate(ret, item_index); }
    HRESULT STDMETHODCALLTYPE get_Column(int* ret) override { NativeCallbackScope callback; return CellCoordinate(ret, item_column); }
    HRESULT STDMETHODCALLTYPE get_RowSpan(int* ret) override { NativeCallbackScope callback; return CellCoordinate(ret, 1); }
    HRESULT STDMETHODCALLTYPE get_ColumnSpan(int* ret) override { NativeCallbackScope callback; return CellCoordinate(ret, 1); }
    HRESULT CellCoordinate(int* ret, int value) {
        if (!ret) return E_POINTER;
        *ret = 0;
        if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
        *ret = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ContainingGrid(IRawElementProviderSimple** ret) override { NativeCallbackScope callback;
        return get_SelectionContainer(ret);
    }
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
    if (state->root) return state->root;
    auto* node = new UiaNode(std::make_shared<UiaLink>(w, nullptr), true);
    node->host_status = E_PENDING;
    state->root = node; // 先发布根，系统 provider 查询可能引发 COM 重入。
    // 断开时 HWND 已可能销毁，只缓存系统身份，不再访问窗口。
    node->host_status = UiaHostProviderFromHwnd(w->hwnd_, &node->host_provider);
    if (FAILED(node->host_status))
        Log(LogLevel::Warn, L"UiaHostProviderFromHwnd failed: 0x%08lx",
            static_cast<unsigned long>(node->host_status));
    return node;
}

UiaNode* UiaNode::NodeFor(WindowImpl* w, Control* c) {
    if (!c) return RootOf(w);
    auto& nodes = StateOf(w)->nodes;
    if (const auto found = nodes.find(c); found != nodes.end()) return found->second;
    auto node = std::make_unique<UiaNode>(std::make_shared<UiaLink>(w, c, RootOf(w)), false);
    const auto [slot, inserted] = nodes.emplace(c, node.get());
    if (inserted) return node.release();
    return slot->second;
}

UiaNode* UiaNode::GhostOf(WindowImpl* w, Control* host, int index) {
    return new UiaNode(NodeFor(w, host)->link, false, index);
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
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    VariantInit(ret);
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    const bool enabled = !Target() || Target()->Enabled();
    if (id == UIA_ValueValuePropertyId && Target()) {
        SetBstr(ret, IsCell() ? Target()->AutomationCellValue(item_index, item_column) : Target()->AutomationValue());
        return S_OK;
    }
    if (id == UIA_ValueIsReadOnlyPropertyId && Target()) {
        SetBool(ret, IsCell() ? Target()->AutomationCellReadOnly(item_index, item_column) : Target()->AutomationIsReadOnly());
        return S_OK;
    }
    if (id == UIA_GridRowCountPropertyId && Target()) { SetI4(ret, Target()->AutomationItemCount()); return S_OK; }
    if (id == UIA_GridColumnCountPropertyId && Target()) { SetI4(ret, Target()->AutomationColumnCount()); return S_OK; }
    if (IsCell()) {
        if (id == UIA_GridItemRowPropertyId) { SetI4(ret, item_index); return S_OK; }
        if (id == UIA_GridItemColumnPropertyId) { SetI4(ret, item_column); return S_OK; }
        if (id == UIA_GridItemRowSpanPropertyId || id == UIA_GridItemColumnSpanPropertyId) { SetI4(ret, 1); return S_OK; }
    }
    if (id == UIA_NamePropertyId) {
        std::wstring name;
        if (IsCell()) name = Target()->AutomationCellName(item_index, item_column);
        else if (IsGhost() && Target()) name = Target()->AutomationItemName(item_index);
        else if (Target()) name = Target()->AutomationName();
        else if (Impl()) name = Impl()->title_;
        SetBstr(ret, name);
        return S_OK;
    }
    if (id == UIA_ControlTypePropertyId) {
        if (IsCell()) { SetI4(ret, UIA_DataItemControlTypeId); return S_OK; }
        AutomationControlType type = AutomationControlType::Window;
        if (IsGhost()) type = AutomationControlType::List;
        else if (Target()) type = Target()->AutomationType();
        SetI4(ret, ControlTypeId(type));
        return S_OK;
    }
    if (id == UIA_HelpTextPropertyId) {
        if (Target()) SetBstr(ret, Target()->AccessibleHelp());
        return S_OK;
    }
    if (id == UIA_IsEnabledPropertyId) {
        SetBool(ret, enabled);
        return S_OK;
    }
    if (id == UIA_IsOffscreenPropertyId && IsCell()) {
        const Rect bounds = Target()->AutomationCellBounds(item_index, item_column);
        SetBool(ret, !Target()->Visible() || bounds.w <= 0.0f || bounds.h <= 0.0f);
        return S_OK;
    }
    if (id == UIA_IsKeyboardFocusablePropertyId) {
        SetBool(ret, !IsGhost() && Impl() && Impl()->UiaFocusable(Target()));
        return S_OK;
    }
    if (id == UIA_HasKeyboardFocusPropertyId) {
        SetBool(ret, !IsGhost() && Target() && Impl() && Impl()->focused_ == Target());
        return S_OK;
    }
    if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
        SetBool(ret, true);
        return S_OK;
    }
    if (id == UIA_IsPasswordPropertyId) {
        SetBool(ret, Target() && !IsGhost() && Target()->AutomationIsPassword());
        return S_OK;
    }
    if (id == UIA_LiveSettingPropertyId) {
        SetI4(ret, Target() && !IsGhost() ? Target()->AutomationLiveSetting() : 0);
        return S_OK;
    }
    if (id == UIA_FrameworkIdPropertyId) {
        SetBstr(ret, L"LUMEN");
        return S_OK;
    }
    if (id == UIA_NativeWindowHandlePropertyId && IsRoot() && Impl() && Impl()->hwnd_) {
        SetI4(ret, static_cast<LONG>(reinterpret_cast<uintptr_t>(Impl()->hwnd_)));
        return S_OK;
    }
    return S_OK;
}

HRESULT UiaNode::get_BoundingRectangle(UiaRect* ret) {
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    *ret = {};
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!Impl() || !Impl()->hwnd_) return S_OK;
    RECT client{};
    GetClientRect(Impl()->hwnd_, &client);
    POINT origin{0, 0};
    ClientToScreen(Impl()->hwnd_, &origin);
    const float scale = Impl()->scale_ > 0.0f ? Impl()->scale_ : 1.0f;
    if (IsRoot() || (IsGhost() && !IsCell()) || !Target()) {
        ret->left = origin.x;
        ret->top = origin.y;
        ret->width = client.right;
        ret->height = client.bottom;
        return S_OK;
    }
    const Rect r = IsCell() ? Target()->AutomationCellBounds(item_index, item_column) : Target()->AbsoluteBounds();
    POINT tl{static_cast<LONG>(r.x * scale), static_cast<LONG>(r.y * scale)};
    ClientToScreen(Impl()->hwnd_, &tl);
    ret->left = tl.x;
    ret->top = tl.y;
    ret->width = r.w * scale;
    ret->height = r.h * scale;
    return S_OK;
}

HRESULT UiaNode::get_FragmentRoot(IRawElementProviderFragmentRoot** ret) {
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    // UIA 断开需要根身份；业务失效不影响身份，也不再触及 impl/control。
    *ret = IsRoot() ? static_cast<IRawElementProviderFragmentRoot*>(this)
                    : link->root_provider;
    if (*ret) (*ret)->AddRef();
    return S_OK;
}

HRESULT UiaNode::Navigate(NavigateDirection dir, IRawElementProviderFragment** ret) {
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!Impl()) return S_OK;
    if (IsGhost()) {
        if (dir == NavigateDirection_Parent && Target()) {
            UiaNode* parent = NodeFor(Impl(), Target());
            parent->AddRef();
            *ret = parent;
        }
        if (IsCell() && (dir == NavigateDirection_NextSibling || dir == NavigateDirection_PreviousSibling)) {
            int column = item_column + (dir == NavigateDirection_NextSibling ? 1 : -1);
            int row = item_index;
            const int columns = Target()->AutomationColumnCount();
            if (column >= columns) { column = 0; ++row; }
            if (column < 0) { column = columns - 1; --row; }
            if (row >= 0 && row < Target()->AutomationItemCount()) {
                auto* cell = GhostOf(Impl(), Target(), row);
                cell->item_column = column;
                *ret = cell;
            }
        }
        return S_OK;
    }
    if (Target() && (Patterns() & kPatternGrid) &&
        (dir == NavigateDirection_FirstChild || dir == NavigateDirection_LastChild)) {
        const int rows = Target()->AutomationItemCount(), columns = Target()->AutomationColumnCount();
        if (rows > 0 && columns > 0) {
            auto* cell = GhostOf(Impl(), Target(), dir == NavigateDirection_FirstChild ? 0 : rows - 1);
            cell->item_column = dir == NavigateDirection_FirstChild ? 0 : columns - 1;
            *ret = cell;
        }
        return S_OK;
    }
    std::vector<Control*> siblings;
    Control* parent = Impl()->UiaParentOf(Target());
    if (Target()) CollectChildren(Impl(), parent, siblings);
    else CollectRootChildren(Impl(), siblings);

    auto wrap = [&](Control* c) {
        if (!c) return;
        UiaNode* node = NodeFor(Impl(), c);
        node->AddRef();
        *ret = node;
    };

    if (dir == NavigateDirection_Parent) {
        if (!Target()) return S_OK;
        if (parent) wrap(parent);
        else {
            UiaNode* root_node = RootOf(Impl());
            root_node->AddRef();
            *ret = root_node;
        }
        return S_OK;
    }
    std::vector<Control*> kids;
    CollectChildren(Impl(), Target(), kids);
    if (dir == NavigateDirection_FirstChild) {
        if (!kids.empty()) wrap(kids.front());
        return S_OK;
    }
    if (dir == NavigateDirection_LastChild) {
        if (!kids.empty()) wrap(kids.back());
        return S_OK;
    }
    if (!Target()) return S_OK;
    ptrdiff_t at = -1;
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == Target()) {
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
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!Impl() || !Impl()->hwnd_) return S_OK;
    POINT screen{static_cast<LONG>(x), static_cast<LONG>(y)};
    POINT client = screen;
    ScreenToClient(Impl()->hwnd_, &client);
    const float scale = Impl()->scale_ > 0.0f ? Impl()->scale_ : 1.0f;
    Control* hit = Impl()->HitTest({static_cast<float>(client.x) / scale,
                                  static_cast<float>(client.y) / scale});
    UiaNode* node = hit ? NodeFor(Impl(), hit) : RootOf(Impl());
    node->AddRef();
    *ret = node;
    return S_OK;
}

HRESULT UiaNode::GetFocus(IRawElementProviderFragment** ret) {
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!Impl()) return S_OK;
    UiaNode* node = Impl()->focused_ ? NodeFor(Impl(), Impl()->focused_) : RootOf(Impl());
    node->AddRef();
    *ret = node;
    return S_OK;
}

HRESULT UiaNode::GetSelection(SAFEARRAY** ret) {
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!Target() || IsGhost()) return S_OK;
    const int index = Target()->AutomationSelectedIndex();
    if (index < 0) {
        *ret = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return *ret ? S_OK : E_OUTOFMEMORY;
    }
    UiaNode* item = GhostOf(Impl(), Target(), index);
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
    NativeCallbackScope callback;
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!Target()) return UIA_E_INVALIDOPERATION;
    if (!Target()->Enabled()) return UIA_E_ELEMENTNOTENABLED;
    if (IsGhost()) {
        return Target()->AutomationSelectIndex(item_index) ? S_OK : E_INVALIDARG;
    }
    if (Patterns() & kPatternSelectionItem) {
        if (Target()->AutomationToggleState() == 1) return S_OK;
        return Target()->AutomationToggle() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    return UIA_E_INVALIDOPERATION;
}

HRESULT UiaNode::get_IsSelected(BOOL* ret) {
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    *ret = {};
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    *ret = FALSE;
    if (!Target()) return S_OK;
    if (IsGhost()) {
        *ret = Target()->AutomationSelectedIndex() == item_index ? TRUE : FALSE;
    } else {
        *ret = Target()->AutomationToggleState() == 1 ? TRUE : FALSE;
    }
    return S_OK;
}

HRESULT UiaNode::get_SelectionContainer(IRawElementProviderSimple** ret) {
    NativeCallbackScope callback;
    if (!ret) return E_POINTER;
    *ret = nullptr;
    if (!Available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!Impl() || !Target()) return S_OK;
    Control* host = IsGhost() ? Target() : Impl()->UiaParentOf(Target());
    UiaNode* node = host ? NodeFor(Impl(), host) : RootOf(Impl());
    node->AddRef();
    *ret = node;
    return S_OK;
}

LRESULT WindowImpl::UiaGetObject(WPARAM wparam, LPARAM lparam) {
    if (uia_shutting_down_) return 0;
    if (!hwnd_ || static_cast<LONG>(lparam) != static_cast<LONG>(UiaRootObjectId)) {
        return DefWindowProcW(hwnd_, WM_GETOBJECT, wparam, lparam);
    }
    UiaNode* root = UiaNode::RootOf(this);
    return UiaReturnRawElementProvider(hwnd_, wparam, lparam,
                                       static_cast<IRawElementProviderSimple*>(root));
}

namespace {
// 调用者交出一个已有引用：成功即Release，失败由pending持有供STA下次重试。
void DisconnectOwned(UiaNode* node) {
    const HRESULT status = UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(node));
    if (SUCCEEDED(status)) { node->Release(); return; }
    Log(LogLevel::Warn, L"UiaDisconnectProvider failed: 0x%08lx", static_cast<unsigned long>(status));
    if (node->pending) { node->Release(); return; }
    node->pending = true;
    node->pending_next = g_pending_disconnect;
    g_pending_disconnect = node;
}

void DisconnectLink(const std::shared_ptr<UiaLink>& link) {
    // 先给整条非拥有链临时引用，防COM断开重入释放后续节点。
    for (auto* node = link->head; node; node = node->link_next) node->AddRef();
    for (auto* node = link->head; node;) {
        auto* next = node->link_next;
        DisconnectOwned(node);
        node = next;
    }
}
}

bool UiaCanShutdown() {
    // 必须在provider所属UI/STA线程；失败引用保留，下一次调用继续尝试。
    auto* node = g_pending_disconnect;
    g_pending_disconnect = nullptr;
    while (node) {
        auto* next = node->pending_next;
        node->pending = false;
        node->pending_next = nullptr;
        DisconnectOwned(node);
        node = next;
    }
    return g_live_providers.load() == 0;
}

void WindowImpl::UiaShutdown() {
    uia_shutting_down_ = true;
    auto* state = static_cast<UiaState*>(uia_state_);
    uia_state_ = nullptr;
    if (!state) return;
    // 断开前使所有root、普通节点及共享link的Ghost一起失效。
    for (auto& pair : state->nodes) {
        if (!pair.second) continue;
        pair.second->link->impl = nullptr;
        pair.second->link->control = nullptr;
    }
    if (state->root) {
        state->root->link->impl = nullptr;
        state->root->link->control = nullptr;
    }
    for (auto& pair : state->nodes) {
        if (!pair.second) continue;
        DisconnectLink(pair.second->link);
        pair.second->Release();
    }
    if (state->root) {
        DisconnectLink(state->root->link);
        state->root->Release();
    }
    delete state;
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
    auto* node = it->second;
    state->nodes.erase(it);
    if (!node) return;
    node->link->impl = nullptr;
    node->link->control = nullptr;
    DisconnectLink(node->link);
    node->Release();
}

} // namespace lumen

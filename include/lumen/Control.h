// lumen/Control.h — 控件基类。状态与动画归控件所有，窗口负责输入路由与重绘调度。
// Events: 无（本头无订阅事件）
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Signal.h"
#include "Text.h"
#include "Theme.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if !defined(LUMEN_DEBUG_CHECKS)
#  if defined(_DEBUG)
#    define LUMEN_DEBUG_CHECKS 1
#  else
#    define LUMEN_DEBUG_CHECKS 0
#  endif
#endif

namespace lumen {

class Painter;
class Window;
class Panel;
class ToolTip;
class Menu;
class Control;

struct WeakLink {
    void (*clear)(void*) = nullptr;
    void* slot = nullptr;
    WeakLink* next = nullptr;
    WeakLink* prev = nullptr;
    Control* host = nullptr;
};

// 指针形态（公共 API 不暴露 HCURSOR，窗口在 WM_SETCURSOR 里映射为系统光标）。
enum class CursorShape { Arrow, IBeam, Hand, SizeWE, SizeNS };

// 无障碍控件类型。映射到 UIA Control Type；公共头不暴露 UIAutomation.h。
enum class AutomationControlType : uint8_t {
    Pane,
    Group,
    Button,
    CheckBox,
    RadioButton,
    Edit,
    Slider,
    ProgressBar,
    List,
    DataGrid,
    ComboBox,
    Tab,
    Tree,
    Text,
    Hyperlink,
    Image,
    Header,
    StatusBar,
    ToolTip,
    Separator,
    SplitButton,
    MenuBar,
    Window,
    Custom
};

inline constexpr uint32_t kPatternInvoke = 1u << 0;
inline constexpr uint32_t kPatternToggle = 1u << 1;
inline constexpr uint32_t kPatternValue = 1u << 2;
inline constexpr uint32_t kPatternRange = 1u << 3;
inline constexpr uint32_t kPatternExpand = 1u << 4;
inline constexpr uint32_t kPatternSelection = 1u << 5;
inline constexpr uint32_t kPatternSelectionItem = 1u << 6;

class Control {
public:
    Control();
    virtual ~Control();
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;
    Control(Control&& other) noexcept;
    Control& operator=(Control&& other) noexcept;

    bool Visible() const noexcept { return visible_; }
    Control& Visible(bool value);
    bool Enabled() const noexcept { return enabled_; }
    Control& Enabled(bool value);
    bool HasFocus() const noexcept { return focused_; }
    // 把键盘焦点放到本控件。未入窗口树时空操作。
    Control& Focus();
    Panel* Parent() const noexcept { return parent_; }

    // 相对父容器的位置（DIP）。Row/Column/Grid 会在 Arrange 时覆盖此值。
    // 仅手动 Panel 与装饰块（1px 线、骨架）需要调用方设定。
    const Rect& Bounds() const noexcept { return bounds_; }
    Control& SetBounds(const Rect& r);

    // 悬停提示：鼠标在控件上静置约 600ms 后由窗口层以 overlay 绘制。
    // 空串 / 空内容表示无提示；点击关闭钮/点击别处/滚轮即隐藏。指针在提示气泡上仍保持显示。
    // 文本含换行时首行为标题（BodyStrong 辉光），其余为换行正文。
    // 字符串与自定义内容互斥：设其一会清掉另一。自定义内容不进宿主 children_。
    Control& ToolTip(std::wstring_view text);
    Control& ToolTip(std::unique_ptr<class ToolTip> content);
    const std::wstring& ToolTip() const noexcept { return tooltip_; }
    class ToolTip* ToolTipContent() const noexcept { return tooltip_content_.get(); }
    bool HasToolTip() const noexcept { return !tooltip_.empty() || tooltip_content_ != nullptr; }
    // 右键菜单：空菜单表示清除。未入窗口树时 ShowContextMenu 静默失败。
    Control& ContextMenu(Menu menu);
    bool HasContextMenu() const noexcept { return has_context_menu_; }
    virtual bool ShowContextMenu(Point window_dip);
    // 所在窗口；未加入窗口树时返回 nullptr。
    Window* WindowOf() const noexcept { return window_; }
    Control& AccessibleName(std::wstring_view name);
    const std::wstring& AccessibleName() const noexcept { return accessible_name_; }
    // UIA：类型/模式由控件头覆写；提供程序在 src/core/uia.cpp，不把 COM 类型暴露到公共头。
    virtual AutomationControlType AutomationType() const noexcept {
        return AutomationControlType::Pane;
    }
    virtual uint32_t AutomationPatterns() const noexcept { return 0; }
    virtual std::wstring AutomationName() const;
    virtual bool AutomationInvoke() { return false; }
    // Toggle：-1 无此模式，0 关，1 开，2 中间态。
    virtual int AutomationToggleState() const noexcept { return -1; }
    virtual bool AutomationToggle() { return false; }
    virtual std::wstring AutomationValue() const { return {}; }
    virtual bool AutomationSetValue(std::wstring_view) { return false; }
    virtual bool AutomationIsReadOnly() const noexcept { return true; }
    virtual bool AutomationIsPassword() const noexcept { return false; }
    virtual double AutomationRangeValue() const { return 0.0; }
    virtual double AutomationRangeMin() const noexcept { return 0.0; }
    virtual double AutomationRangeMax() const noexcept { return 0.0; }
    virtual double AutomationRangeSmall() const noexcept { return 1.0; }
    virtual double AutomationRangeLarge() const noexcept { return 10.0; }
    virtual bool AutomationSetRange(double) { return false; }
    virtual bool AutomationCanSelectMultiple() const noexcept { return false; }
    // Expand：-1 无，0 折叠，1 展开。
    virtual int AutomationExpandState() const noexcept { return -1; }
    virtual bool AutomationExpand() { return false; }
    virtual bool AutomationCollapse() { return false; }
    virtual int AutomationSelectedIndex() const noexcept { return -1; }
    virtual int AutomationItemCount() const noexcept { return 0; }
    virtual bool AutomationSelectIndex(int) { return false; }
    virtual std::wstring AutomationItemName(int) const { return {}; }
    // LiveSetting：0 Off，1 Polite，2 Assertive。
    virtual int AutomationLiveSetting() const noexcept { return 0; }

    // 主轴弹性：权重大于 0 时 flex-basis 为 0，按权重分配剩余空间（两列 Grow(1) 等宽）。
    Control& Grow(float weight = 1.0f);
    float GrowWeight() const noexcept { return grow_weight_; }
    // 交叉轴拉满：父级 AlignCross(Start) 时仍铺满列宽/行高，不必再套一层 Row。
    Control& FillCross(bool value = true);
    bool FillsCross() const noexcept { return fill_cross_; }
    // 尺寸钳制与外边距（DIP）。max 的 0 表示不限制。布局容器按 margin box 排，Arrange 再内缩。
    Control& MinSize(Size size);
    Control& MaxSize(Size size);
    Control& Margin(float uniform);
    Control& Margin(float horizontal, float vertical);
    Control& Margin(Thickness thickness);
    Size MinSize() const noexcept { return min_size_; }
    Size MaxSize() const noexcept { return max_size_; }
    Thickness Margin() const noexcept { return margin_; }
    // 仅键盘导航时为真（:focus-visible）。无窗口（离屏测试）时等同 HasFocus。
    bool FocusVisible() const noexcept;
    // 容器判定：替代热路径 dynamic_cast<Panel*>。
    virtual class Panel* AsPanel() noexcept { return nullptr; }
    virtual const class Panel* AsPanel() const noexcept { return nullptr; }
    // 最近一次布局后的窗口客户区绝对矩形（DIP）。
    const Rect& AbsoluteBounds() const noexcept { return absolute_; }
    Size DesiredSize() const noexcept { return desired_; }

    // 鼠标聚光（LUMEN 卡片光感）：开启后 Draw 中可读 spotlight_t_/spotlight_pos_
    // 获得渐显强度与平滑跟随的光斑位置（局部 DIP）。配合 Painter::DrawSpotlight 使用。
    // 光斑在鼠标落入本控件矩形时当帧点亮（含命中子控件，等效 CSS :hover 祖先）；
    // 离开时才对 spotlight_t_ 做淡出。
    bool Spotlight() const noexcept { return spotlight_enabled_; }
    Control& Spotlight(bool enabled);
    Control& BindVisible(Property<bool>& p);
    Control& BindEnabled(Property<bool>& p);
    static void SetDebugHandler(void (*fn)(const wchar_t*));
    void AttachWeak(WeakLink* link) noexcept;
    void DetachWeak(WeakLink* link) noexcept;

    // 声明式构建：Children 把栈上临时对象迁到堆后，把堆地址写回 out。
    // 必须经 Children/Add 提交；不要对已入树控件取地址再 move。
    template <typename Self>
    Self& Ref(Self*& out) {
        static_assert(std::is_base_of_v<Control, Self>, "Ref 仅用于 Control 派生类型");
        ref_slot_ = &out;
        bind_ref_ = [](void* slot, Control* heap) {
            *static_cast<Self**>(slot) = static_cast<Self*>(heap);
        };
        return static_cast<Self&>(*this);
    }

    // 子树 token / 密度。Draw 与 Measure 经 EffectiveTheme 合成；无覆盖时返回 base。
    Control& Style(const ThemeOverride& o);
    Control& Density(lumen::Density d);
    Theme EffectiveTheme(const Theme& base) const;

protected:
    friend class Window;
    friend class Panel;
    static void DebugTrap(const wchar_t* message);
    void CommitRef() noexcept;
    void AbandonRef() noexcept;
    friend void DrawControlTree(Painter& painter, const Theme& theme, Control* root);
    friend void DrawControlTree(Painter& painter, const Theme& theme, Control* root,
                                const Rect& clip);

    // 布局：Measure 计算期望尺寸，Arrange 得到绝对矩形并排布子级。
    virtual Size Measure(Size available, const Theme& theme);
    virtual void Arrange(const Rect& absolute);
    // 帧前资源准备。可在设备首次出现或恢复后创建 GPU 资源；常态帧必须快速返回。
    virtual void Prepare(Painter& painter) { (void)painter; }
    virtual void Draw(Painter& painter, const Theme& theme) = 0;

    // 输入（局部坐标，DIP）。返回 true 表示事件已处理。
    virtual void OnMouseEnter();
    virtual void OnMouseLeave();
    virtual void OnMouseMove(Point local, uint32_t buttons) { (void)local; (void)buttons; }
    virtual void OnMouseDown(Point local, uint32_t buttons) { (void)local; (void)buttons; }
    virtual void OnMouseUp(Point local, uint32_t buttons) { (void)local; (void)buttons; }
    virtual void OnMouseDoubleClick(Point local) { (void)local; }
    virtual bool OnWheel(float delta) {
        (void)delta;
        return false;
    }
    virtual bool OnHWheel(float delta) {
        (void)delta;
        return false;
    }
    // 触摸拖动平移：ScrollViewer/ListView 覆写。dx/dy 为手指位移（DIP）。
    virtual bool CanPan() const noexcept { return false; }
    virtual void PanBy(float dx, float dy) {
        (void)dx;
        (void)dy;
    }
    virtual void PanFling(float vx, float vy) {
        (void)vx;
        (void)vy;
    }
    // 文本选区/滑块拖动等：触摸移动时不要被祖先滚动抢走。
    virtual bool PrefersDragOverPan() const noexcept { return false; }
    bool TouchInput() const noexcept;
    // 盖在子级之上的命中层（滚动条轨道）。为真时 HitTest 不再下钻子级。
    virtual bool CapturesOverlay(Point p) const {
        (void)p;
        return false;
    }
    virtual bool OnKey(uint32_t vk) { (void)vk; return false; }
    virtual bool OnChar(wchar_t ch) { (void)ch; return false; }
    virtual void OnFocusChanged(bool focused);
    virtual bool Focusable() const noexcept { return false; }
    // 插入符顶点与行高（窗口 DIP）。DirectComposition 没有系统 caret，
    // 不报这个位置的话 IME 候选框会落到屏幕 (0,0)。
    virtual bool ImeCaret(Point& window_dip, float& height_dip) const {
        (void)window_dip;
        (void)height_dip;
        return false;
    }
    // 为真时窗口隐藏系统组字窗，由控件把预编辑串画进文本流（候选窗仍走系统 IME）。
    virtual bool ImeInline() const noexcept { return false; }
    virtual bool ImeComposing() const noexcept { return false; }
    virtual void OnImeCompose(std::wstring_view text, size_t cursor, std::string_view attributes) {
        (void)text;
        (void)cursor;
        (void)attributes;
    }
    virtual void OnImeCommit(std::wstring_view text) { (void)text; }
    virtual void OnImeEnd() {}
    // 命中测试穿透（如静态文本）：鼠标事件落到其下的控件。
    virtual bool HitTransparent() const noexcept { return false; }
    // OLE 文件拖放（CF_HDROP）。窗口命中本控件或其祖先后走这套虚函数。
    virtual bool AcceptsFileDrop() const noexcept { return false; }
    virtual std::vector<std::wstring> FilterFileDrop(std::vector<std::wstring> paths) const {
        (void)paths;
        return {};
    }
    virtual void OnFileDrag(bool over) { (void)over; }
    virtual void OnFileDrop(std::vector<std::wstring> paths) { (void)paths; }
    // OLE 文本拖放（CF_UNICODETEXT）。window_dip 为窗口客户区坐标。
    virtual bool AcceptsTextDrop() const noexcept { return false; }
    virtual void OnTextDrop(std::wstring_view text, Point window_dip) {
        (void)text;
        (void)window_dip;
    }
    // 本控件命中区内的指针形态（悬停时 WM_SETCURSOR 查询，局部 DIP 坐标）。
    virtual CursorShape CursorAt(Point local) const {
        (void)local;
        return CursorShape::Arrow;
    }
    // 聚光卡是否在本控件矩形垫回碳底。默认与命中实体一致；勾选/开关等贴在卡片面上。
    virtual bool BlocksCardSpotlight() const noexcept { return !HitTransparent(); }
    // 垫底圆角。胶囊/圆返回半短边，避免 radius_control 圆角矩形从轮廓外露出来。
    virtual float ChromeRadius(const Theme& theme) const noexcept { return theme.radius_control; }

    // 动画时钟：返回 true 表示仍需继续每帧回调。
    // LUMEN 状态切换多为瞬时；聚光渐显/跟随与个别动画（开关滑块、进度不定态、
    // 平滑滚动）使用此时钟。
    virtual bool OnAnimate(float dt_seconds);

    // 子树整体补写 window_（脱离窗口构建的子树挂进已绑定面板时调用）。
    void BindWindowDeep();

    void Invalidate();            // 请求重绘（按 absolute_ 外扩脏矩形；无窗口则空操作）
    Rect DirtyBounds() const noexcept;
    void Animate();               // 请求动画时钟（空闲后随 Present 停拍）
    void RelayoutParent();        // 影响布局的属性变化后调用
    // 单行文本：优先本窗口 LumaText 步进宽，并预留与 DrawText 相同的墨迹外扩。
    Size MeasureText(std::wstring_view text, TextRole role, float max_width = 0.0f) const;
    // 多行换行高度：与 DrawTextWrapped 同一套，优先 LumaText。
    float MeasureWrapped(std::wstring_view text, TextRole role, float width) const;
    void AssertUiThread() const;
    void* NativeWindow() const;   // HWND，用于剪贴板/弹窗定位

    // 0..1 指数平滑。到位返回 false。（仅用于动画型控件，状态切换不走此路径）
    // 有时程/曲线/弹簧见 lumen/Animate.h（Tween / SpringMotion）。
    // 读取所在窗口 Theme::motion_scale：0 时直接到位。
    bool EaseTo(float& value, float target, float dt, float speed = 12.0f,
                float epsilon = 0.002f);
    float MotionScale() const noexcept;
    // 键盘焦点环：按 focus_ring_t_ 生长；无窗口时若 FocusVisible 则满强度。
    void PaintFocusRing(Painter& painter, const Theme& theme, const Rect& r,
                        float radius) const;

    // 推进聚光渐显（基类 OnAnimate 自动调用）。光斑位置由鼠标消息当帧写入，不走时钟。
    bool EaseSpotlight(float dt);
    // 聚光中心（窗口 DIP 坐标）。有窗口且鼠标在卡片内时用 mouse_local_（连续跟随）；
    // 离屏或已离开则用 spotlight_pos_（离屏居中 / 淡出时冻结最后位置）。
    Point SpotlightCenter() const {
        if (!window_) {
            return {absolute_.x + absolute_.w * 0.5f, absolute_.y + absolute_.h * 0.5f};
        }
        const Point& local = spotlight_inside_ ? mouse_local_ : spotlight_pos_;
        return {absolute_.x + local.x, absolute_.y + local.y};
    }

    // 输入状态（瞬时生效，无过渡）
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;

    // 聚光状态。mouse_local_ 由 WindowImpl 在鼠标消息里写入（友元），绘制当帧读取，
    // 不经动画时钟，避免慢一拍。spotlight_pos_ 在离开时冻结，供淡出。
    // spotlight_inside_ 在鼠标落入本控件绝对矩形时为真（含子控件命中，等效 CSS :hover 祖先）。
    Point mouse_local_{0.0f, 0.0f};
    Point spotlight_pos_{0.0f, 0.0f};
    float spotlight_t_ = 0.0f;
    bool spotlight_enabled_ = false;
    bool spotlight_inside_ = false;

    Rect bounds_;        // 相对父级
    Rect absolute_;      // Arrange 阶段算出
    Size desired_;
    float grow_weight_ = 0.0f;
    bool fill_cross_ = false;
    Size min_size_{};
    Size max_size_{};
    Thickness margin_{};
    float focus_ring_t_ = 0.0f;
    bool visible_ = true;
    bool enabled_ = true;
    std::wstring accessible_name_;
    std::wstring tooltip_;
    std::unique_ptr<class ToolTip> tooltip_content_;
    std::unique_ptr<Menu> context_menu_;
    bool has_context_menu_ = false;
    Panel* parent_ = nullptr;
    Window* window_ = nullptr;   // 所在窗口（根控件由窗口注入，随容器下传）

private:
    friend class WindowImpl;
    template <class T>
    friend class WeakRef;
    mutable bool anim_listed_ = false;
    void* ref_slot_ = nullptr;
    void (*bind_ref_)(void*, Control*) = nullptr;
    std::unique_ptr<ThemeOverride> style_;
    WeakLink* weak_head_ = nullptr;
    ScopedConnection bind_visible_;
    ScopedConnection bind_enabled_;
    void StealFrom(Control& other) noexcept;
};

template <class T>
class WeakRef {
public:
    WeakRef() noexcept = default;
    WeakRef(T* p) { Reset(p); }
    ~WeakRef() { Reset(nullptr); }
    WeakRef(const WeakRef&) = delete;
    WeakRef& operator=(const WeakRef&) = delete;
    WeakRef(WeakRef&& other) noexcept { MoveFrom(other); }
    WeakRef& operator=(WeakRef&& other) noexcept {
        if (this == &other) return *this;
        Reset(nullptr);
        MoveFrom(other);
        return *this;
    }
    WeakRef& operator=(T* p) {
        Reset(p);
        return *this;
    }
    void Reset(T* p = nullptr) {
        if (link_.host) {
            link_.host->DetachWeak(&link_);
            link_.host = nullptr;
        }
        ptr_ = p;
        if (!p) return;
        link_.clear = [](void* s) { *static_cast<T**>(s) = nullptr; };
        link_.slot = &ptr_;
        p->AttachWeak(&link_);
    }
    T* Get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    void MoveFrom(WeakRef& other) noexcept {
        ptr_ = other.ptr_;
        link_ = other.link_;
        other.ptr_ = nullptr;
        other.link_ = {};
        if (!ptr_ || !link_.host) return;
        if (link_.prev) link_.prev->next = &link_;
        else link_.host->weak_head_ = &link_;
        if (link_.next) link_.next->prev = &link_;
        link_.slot = &ptr_;
        link_.clear = [](void* s) { *static_cast<T**>(s) = nullptr; };
    }

    T* ptr_ = nullptr;
    WeakLink link_{};
};

} // namespace lumen

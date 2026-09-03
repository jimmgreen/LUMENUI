#include "lumen/Control.h"
#include "lumen/Animate.h"
#include "lumen/Menu.h"
#include "lumen/Painter.h"
#include "lumen/Panel.h"
#include "lumen/ToolTip.h"
#include "lumen/Window.h"
#include "window_impl.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace lumen {

namespace {
void (*g_debug_handler)(const wchar_t*) = nullptr;
}

Control::Control() = default;

void Control::StealFrom(Control& o) noexcept {
    hovered_ = o.hovered_;
    o.hovered_ = false;
    pressed_ = o.pressed_;
    o.pressed_ = false;
    focused_ = o.focused_;
    o.focused_ = false;
    mouse_local_ = o.mouse_local_;
    spotlight_pos_ = o.spotlight_pos_;
    spotlight_t_ = o.spotlight_t_;
    o.spotlight_t_ = 0.0f;
    spotlight_enabled_ = o.spotlight_enabled_;
    o.spotlight_enabled_ = false;
    spotlight_inside_ = o.spotlight_inside_;
    o.spotlight_inside_ = false;
    bounds_ = o.bounds_;
    absolute_ = o.absolute_;
    desired_ = o.desired_;
    grow_weight_ = o.grow_weight_;
    o.grow_weight_ = 0.0f;
    fill_cross_ = o.fill_cross_;
    o.fill_cross_ = false;
    min_size_ = o.min_size_;
    max_size_ = o.max_size_;
    margin_ = o.margin_;
    focus_ring_t_ = o.focus_ring_t_;
    o.focus_ring_t_ = 0.0f;
    visible_ = o.visible_;
    enabled_ = o.enabled_;
    accessible_name_ = std::move(o.accessible_name_);
    tooltip_ = std::move(o.tooltip_);
    tooltip_content_ = std::move(o.tooltip_content_);
    context_menu_ = std::move(o.context_menu_);
    has_context_menu_ = o.has_context_menu_;
    o.has_context_menu_ = false;
    parent_ = o.parent_;
    o.parent_ = nullptr;
    window_ = o.window_;
    o.window_ = nullptr;
    anim_listed_ = o.anim_listed_;
    o.anim_listed_ = false;
    ref_slot_ = o.ref_slot_;
    o.ref_slot_ = nullptr;
    bind_ref_ = o.bind_ref_;
    o.bind_ref_ = nullptr;
    style_ = std::move(o.style_);
    weak_head_ = o.weak_head_;
    o.weak_head_ = nullptr;
    for (WeakLink* link = weak_head_; link; link = link->next) link->host = this;
    bind_visible_ = std::move(o.bind_visible_);
    bind_enabled_ = std::move(o.bind_enabled_);
}

Control::Control(Control&& o) noexcept {
    if (o.parent_) DebugTrap(L"LUMEN_CHECK: move of in-tree Control");
    StealFrom(o);
}

Control& Control::operator=(Control&& o) noexcept {
    if (this == &o) return *this;
    if (parent_ || o.parent_) DebugTrap(L"LUMEN_CHECK: move of in-tree Control");
    StealFrom(o);
    return *this;
}

Control::~Control() {
    while (weak_head_) {
        WeakLink* link = weak_head_;
        weak_head_ = link->next;
        if (link->clear && link->slot) link->clear(link->slot);
        link->host = nullptr;
        link->next = nullptr;
        link->prev = nullptr;
        link->slot = nullptr;
        link->clear = nullptr;
    }
    if (bind_ref_ && ref_slot_) bind_ref_(ref_slot_, nullptr);
    if (tooltip_content_) WindowImpl::ForgetTree(tooltip_content_.get());
}

void Control::SetDebugHandler(void (*fn)(const wchar_t*)) { g_debug_handler = fn; }

void Control::AttachWeak(WeakLink* link) noexcept {
    if (!link) return;
    link->host = this;
    link->prev = nullptr;
    link->next = weak_head_;
    if (weak_head_) weak_head_->prev = link;
    weak_head_ = link;
}

void Control::DetachWeak(WeakLink* link) noexcept {
    if (!link || link->host != this) return;
    if (link->prev) link->prev->next = link->next;
    else weak_head_ = link->next;
    if (link->next) link->next->prev = link->prev;
    link->host = nullptr;
    link->next = nullptr;
    link->prev = nullptr;
}

Control& Control::BindVisible(Property<bool>& p) {
    Visible(p.Get());
    bind_visible_ = ScopedConnection(p.OnChanged([this](const bool& v) { Visible(v); }));
    return *this;
}

Control& Control::BindEnabled(Property<bool>& p) {
    Enabled(p.Get());
    bind_enabled_ = ScopedConnection(p.OnChanged([this](const bool& v) { Enabled(v); }));
    return *this;
}

void Control::CommitRef() noexcept {
    if (bind_ref_ && ref_slot_) bind_ref_(ref_slot_, this);
}

void Control::AbandonRef() noexcept {
    bind_ref_ = nullptr;
    ref_slot_ = nullptr;
}

void Control::AssertUiThread() const {
    if (window_ && !window_->IsUiThread()) DebugTrap(L"LUMEN_CHECK: control mutated off the UI thread");
}

void Control::DebugTrap(const wchar_t* message) {
    if (g_debug_handler) {
        g_debug_handler(message);
        return;
    }
#if LUMEN_DEBUG_CHECKS
    if (message) OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
    __debugbreak();
#else
    (void)message;
#endif
}

Control& Control::Style(const ThemeOverride& o) {
    if (!style_) style_ = std::make_unique<ThemeOverride>(o);
    else *style_ = o;
    RelayoutParent();
    Invalidate();
    return *this;
}

Control& Control::Density(lumen::Density d) {
    ThemeOverride o = style_ ? *style_ : ThemeOverride{};
    o.density = d;
    return Style(o);
}

Theme Control::EffectiveTheme(const Theme& base) const {
    const ThemeOverride* chain[24];
    int n = 0;
    for (const Control* c = this; c && n < 24; c = c->parent_) {
        if (c->style_) chain[n++] = c->style_.get();
    }
    Theme t = base;
    for (int i = n - 1; i >= 0; --i) t = ApplyThemeOverride(t, *chain[i]);
    return t;
}

Control& Control::Visible(bool value) {
    if (visible_ == value) return *this;
    visible_ = value;
    RelayoutParent();
    return *this;
}

Control& Control::Enabled(bool value) {
    if (enabled_ == value) return *this;
    enabled_ = value;
    if (!value) {
        pressed_ = false;
        hovered_ = false;
    }
    Invalidate();
    return *this;
}

Control& Control::SetBounds(const Rect& r) {
    if (bounds_.x == r.x && bounds_.y == r.y && bounds_.w == r.w && bounds_.h == r.h) {
        return *this;
    }
    bounds_ = r;
    RelayoutParent();
    return *this;
}

Control& Control::AccessibleName(std::wstring_view name) {
    accessible_name_ = std::wstring(name);
    return *this;
}

std::wstring Control::AutomationName() const { return accessible_name_; }

Control& Control::Grow(float weight) {
    weight = std::max(0.0f, weight);
    if (grow_weight_ == weight) return *this;
    grow_weight_ = weight;
    RelayoutParent();
    return *this;
}

Control& Control::FillCross(bool value) {
    if (fill_cross_ == value) return *this;
    fill_cross_ = value;
    RelayoutParent();
    return *this;
}

Control& Control::MinSize(Size size) {
    size.w = std::max(0.0f, size.w);
    size.h = std::max(0.0f, size.h);
    if (min_size_.w == size.w && min_size_.h == size.h) return *this;
    min_size_ = size;
    RelayoutParent();
    return *this;
}

Control& Control::MaxSize(Size size) {
    size.w = std::max(0.0f, size.w);
    size.h = std::max(0.0f, size.h);
    if (max_size_.w == size.w && max_size_.h == size.h) return *this;
    max_size_ = size;
    RelayoutParent();
    return *this;
}

Control& Control::Margin(float uniform) { return Margin(Thickness::Uniform(uniform)); }

Control& Control::Margin(float horizontal, float vertical) {
    return Margin(Thickness::HV(horizontal, vertical));
}

Control& Control::Margin(Thickness thickness) {
    thickness.left = std::max(0.0f, thickness.left);
    thickness.top = std::max(0.0f, thickness.top);
    thickness.right = std::max(0.0f, thickness.right);
    thickness.bottom = std::max(0.0f, thickness.bottom);
    if (margin_.left == thickness.left && margin_.top == thickness.top &&
        margin_.right == thickness.right && margin_.bottom == thickness.bottom) {
        return *this;
    }
    margin_ = thickness;
    RelayoutParent();
    return *this;
}

bool Control::FocusVisible() const noexcept {
    if (!focused_) return false;
    if (!window_) return true;
    return WindowImpl::KeyboardFocusOf(window_);
}

Size Control::Measure(Size, const Theme&) {
    return {bounds_.w, bounds_.h};   // 默认：手动定位的控件使用设定尺寸
}

Size Control::MeasureText(std::wstring_view text, TextRole role, float max_width) const {
    return MeasureUiText(text, role, max_width, WindowImpl::LumaOf(window_));
}

float Control::MeasureWrapped(std::wstring_view text, TextRole role, float width) const {
    return MeasureWrappedHeight(text, role, width, WindowImpl::LumaOf(window_));
}

void Control::Arrange(const Rect& absolute) {
    absolute_ = absolute;
}

void Control::OnMouseEnter() {
    hovered_ = true;
    Invalidate();
}

void Control::OnMouseLeave() {
    hovered_ = false;
    pressed_ = false;
    Invalidate();
}

void Control::OnFocusChanged(bool focused) {
    focused_ = focused;
    if (!window_ || MotionScale() <= 0.001f) {
        focus_ring_t_ = FocusVisible() ? 1.0f : 0.0f;
    } else {
        Animate();
    }
    Invalidate();
}

Control& Control::Spotlight(bool enabled) {
    if (spotlight_enabled_ == enabled) return *this;
    spotlight_enabled_ = enabled;
    if (!window_) {
        // 离屏（无窗口）场景直接到位：光斑经 SpotlightCenter() 取卡片居中。
        spotlight_t_ = enabled ? 1.0f : 0.0f;
        spotlight_inside_ = enabled;
    }
    Invalidate();
    return *this;
}

bool Control::EaseSpotlight(float dt) {
    if (spotlight_t_ == 0.0f && !spotlight_inside_) return false;
    const float target = spotlight_enabled_ && spotlight_inside_ ? 1.0f : 0.0f;
    if (MotionScale() <= 0.001f) {
        spotlight_t_ = target;
        return false;
    }
    return EaseTo(spotlight_t_, target, dt, 10.0f);
}

bool Control::OnAnimate(float dt) {
    bool more = false;
    if (spotlight_enabled_) more |= EaseSpotlight(dt);
    const float ring_target = FocusVisible() ? 1.0f : 0.0f;
    if (focus_ring_t_ > 0.001f || ring_target > 0.001f) {
        more |= EaseTo(focus_ring_t_, ring_target, dt, 18.0f);
    }
    return more;
}

float Control::MotionScale() const noexcept {
    if (!window_) return 1.0f;
    return WindowImpl::ThemeOf(window_).motion_scale;
}

bool Control::EaseTo(float& value, float target, float dt, float speed, float epsilon) {
    if (MotionScale() <= 0.001f) {
        value = target;
        return false;
    }
    return ::lumen::EaseTo(value, target, dt, speed, epsilon);
}

void Control::PaintFocusRing(Painter& painter, const Theme& theme, const Rect& r,
                             float radius) const {
    const float t = window_ ? focus_ring_t_ : (FocusVisible() ? 1.0f : 0.0f);
    if (t <= 0.001f) return;
    Color accent = theme.accent;
    accent.a *= t;
    const float grow = t;
    painter.DrawFocusRing(r.Inset(-grow, -grow), radius + grow, accent, theme.focus_ring_width);
}

void Control::Invalidate() {
    AssertUiThread();
    if (!window_) return;
    const Rect dirty = DirtyBounds();
    if (dirty.IsEmpty()) WindowImpl::Invalidate(window_);
    else WindowImpl::InvalidateRegion(window_, dirty);
}

Rect Control::DirtyBounds() const noexcept {
    if (absolute_.IsEmpty()) return {};
    if (spotlight_enabled_) return absolute_;
    return absolute_.Inset(-kDirtyPadDip, -kDirtyPadDip);
}

void Control::Animate() {
    if (window_) WindowImpl::Animate(window_, this);
}

void Control::RelayoutParent() {
    AssertUiThread();
    if (window_) WindowImpl::Relayout(window_);
}

Control& Control::Focus() {
    if (window_) WindowImpl::SetFocusTo(window_, this);
    return *this;
}

void* Control::NativeWindow() const {
    return window_ ? window_->NativeHandle() : nullptr;
}

bool Control::TouchInput() const noexcept {
    return WindowImpl::TouchInputOf(window_);
}

Control& Control::ToolTip(std::wstring_view text) {
    if (window_) window_->Impl()->OnToolTipChanged(this);
    if (tooltip_content_) {
        WindowImpl::ForgetTree(tooltip_content_.get());
        tooltip_content_.reset();
    }
    tooltip_ = std::wstring(text);
    return *this;
}

Control& Control::ToolTip(std::unique_ptr<class ToolTip> content) {
    if (window_) window_->Impl()->OnToolTipChanged(this);
    if (tooltip_content_) WindowImpl::ForgetTree(tooltip_content_.get());
    tooltip_.clear();
    tooltip_content_ = std::move(content);
    if (tooltip_content_) {
        tooltip_content_->parent_ = nullptr;
        tooltip_content_->window_ = window_;
        if (window_) WindowImpl::BindWindowRecursive(tooltip_content_.get(), window_);
    }
    return *this;
}

Control& Control::ContextMenu(Menu menu) {
    if (menu.Empty()) {
        context_menu_.reset();
        has_context_menu_ = false;
        return *this;
    }
    context_menu_ = std::make_unique<Menu>(std::move(menu));
    has_context_menu_ = true;
    return *this;
}

bool Control::ShowContextMenu(Point window_dip) {
    if (!has_context_menu_ || !context_menu_ || !window_) return false;
    context_menu_->Popup(*window_, window_dip);
    return true;
}

// ---- Panel ----

Panel::Panel(Panel&& o) noexcept
    : ControlOf<Panel>(std::move(o)),
      children_(std::move(o.children_)),
      background_(o.background_),
      card_fill_(o.card_fill_),
      card_stroke_(o.card_stroke_),
      card_radius_(o.card_radius_),
      card_style_(o.card_style_),
      use_card_style_(o.use_card_style_),
      shimmer_(o.shimmer_),
      clip_children_(o.clip_children_),
      shimmer_phase_(o.shimmer_phase_),
      click_(std::move(o.click_)) {
    for (auto& c : children_) {
        if (c) c->parent_ = this;
    }
}

Panel& Panel::operator=(Panel&& o) noexcept {
    if (this == &o) return *this;
    ControlOf<Panel>::operator=(std::move(o));
    children_ = std::move(o.children_);
    background_ = o.background_;
    card_fill_ = o.card_fill_;
    card_stroke_ = o.card_stroke_;
    card_radius_ = o.card_radius_;
    card_style_ = o.card_style_;
    use_card_style_ = o.use_card_style_;
    shimmer_ = o.shimmer_;
    clip_children_ = o.clip_children_;
    shimmer_phase_ = o.shimmer_phase_;
    click_ = std::move(o.click_);
    for (auto& c : children_) {
        if (c) c->parent_ = this;
    }
    return *this;
}

Panel::~Panel() = default;

Panel& Panel::Add(std::unique_ptr<Control> child) {
    if (!child) return *this;
    if (child->parent_ != nullptr) {
        Control::DebugTrap(L"LUMEN_CHECK: Add() on a control that already has a parent");
        return *this;
    }
    Control& ref = *child;
    child->CommitRef();
    children_.push_back(std::move(child));
    ref.parent_ = this;
    ref.window_ = window_;
    ref.BindWindowDeep();
    Relayout();
    return *this;
}

// 脱离窗口构建的子树挂进已绑定面板时，把整棵后代的 window_ 补齐；
// 否则后代的 Invalidate/SetFocus/Animate 全部静默空转。
void Control::BindWindowDeep() {
    if (window_) WindowImpl::BindWindowRecursive(this, window_);
}

void Panel::Remove(Control& child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == &child) {
            WindowImpl::ForgetTree(&child);   // 窗口不得留存指向已销毁控件的指针
            children_.erase(it);
            RelayoutParent();
            return;
        }
    }
}

void Panel::Clear() {
    if (children_.empty()) return;
    for (const std::unique_ptr<Control>& child : children_) WindowImpl::ForgetTree(child.get());
    children_.clear();
    RelayoutParent();
}

Panel& Panel::Background(Color color) {
    background_ = color;
    Invalidate();
    return *this;
}

Panel& Panel::Clip(bool value) {
    if (clip_children_ == value) return *this;
    clip_children_ = value;
    Invalidate();
    return *this;
}

Panel& Panel::Card(Color fill, Color stroke, float radius) {
    card_fill_ = fill;
    card_stroke_ = stroke;
    card_radius_ = radius;
    use_card_style_ = false;
    Invalidate();
    return *this;
}

Panel& Panel::Card(CardStyle style, float radius) {
    card_style_ = style;
    card_radius_ = radius;
    use_card_style_ = true;
    if (style == CardStyle::Lumen) Spotlight(true);
    Invalidate();
    return *this;
}

void Panel::SetChildBounds(Control& child, const Rect& r) {
    child.bounds_ = r;
}

bool Panel::ChildVisible(size_t index) const {
    return index < children_.size() && children_[index]->visible_;
}

const Size& Panel::ChildDesired(size_t index) const {
    return children_[index]->desired_;
}

void Panel::SetChildVisibility(size_t index, bool visible) {
    if (index < children_.size()) children_[index]->visible_ = visible;
}

Size Panel::MeasureChildAt(size_t index, Size available, const Theme& theme) {
    if (index >= children_.size()) return {};
    Control& child = *children_[index];
    const Thickness m = child.margin_;
    Size inner = available;
    if (inner.w < 1.0e4f) inner.w = std::max(0.0f, inner.w - m.Horizontal());
    if (inner.h < 1.0e4f) inner.h = std::max(0.0f, inner.h - m.Vertical());
    Size d = child.Measure(inner, child.EffectiveTheme(theme));
    if (child.min_size_.w > 0.0f) d.w = std::max(d.w, child.min_size_.w);
    if (child.min_size_.h > 0.0f) d.h = std::max(d.h, child.min_size_.h);
    if (child.max_size_.w > 0.0f) d.w = std::min(d.w, child.max_size_.w);
    if (child.max_size_.h > 0.0f) d.h = std::min(d.h, child.max_size_.h);
    d.w += m.Horizontal();
    d.h += m.Vertical();
    child.desired_ = d;
    return child.desired_;
}

void Panel::SyncChildAbsolute(size_t index) {
    if (index >= children_.size()) return;
    Control& child = *children_[index];
    const Rect& b = child.bounds_;
    const Size& d = child.desired_;
    const Thickness m = child.margin_;
    const float w = std::max(0.0f, (b.w > 0.5f ? b.w : d.w) - m.Horizontal());
    const float h = std::max(0.0f, (b.h > 0.5f ? b.h : d.h) - m.Vertical());
    child.absolute_ = {absolute_.x + b.x + m.left, absolute_.y + b.y + m.top, w, h};
}

void Panel::ArrangeChildAt(size_t index) {
    if (index >= children_.size()) return;
    Control& child = *children_[index];
    const Rect& b = child.bounds_;
    const Size& d = child.desired_;
    const Thickness m = child.margin_;
    // 宽/高为 0 表示该轴随内容：两列并排时只锁宽度、高度交给 Measure。
    const float w = std::max(0.0f, (b.w > 0.5f ? b.w : d.w) - m.Horizontal());
    const float h = std::max(0.0f, (b.h > 0.5f ? b.h : d.h) - m.Vertical());
    child.Arrange({absolute_.x + b.x + m.left, absolute_.y + b.y + m.top, w, h});
}

Size Panel::Measure(Size available, const Theme& theme) {
    float max_right = 0.0f, max_bottom = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        Control& child = Child(i);
        const Rect& b = child.bounds_;
        // 手动定位面板仍需先测量子树，否则嵌套 StackPanel 的 desired_ 会保持为 0，
        // Arrange 阶段所有内容会落在同一位置。
        const Size child_available{b.w > 0.0f ? b.w : available.w,
                                   b.h > 0.0f ? b.h : available.h};
        const Size desired = MeasureChildAt(i, child_available, theme);
        max_right = std::max(max_right, b.x + std::max(b.w, desired.w));
        max_bottom = std::max(max_bottom, b.y + std::max(b.h, desired.h));
    }
    // 无子级的装饰块（分隔线、骨架）以 SetBounds 为期望尺寸。
    return {std::max(max_right, bounds_.w), std::max(max_bottom, bounds_.h)};
}

void Panel::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        ArrangeChildAt(i);
    }
}

void Panel::Draw(Painter& painter, const Theme& theme) {
    if (use_card_style_) {
        switch (card_style_) {
        case CardStyle::Lumen:
            // 聚光卡片：carbon 底 + 鼠标跟随光斑 + 边缘折射光环
            painter.FillRoundedRect(absolute_, card_radius_, theme.fill_input);
            DrawSpotlight(painter, theme, absolute_, card_radius_, SpotlightCenter(),
                          spotlight_t_);
            AvoidControls(painter, theme);
            return;
        case CardStyle::Input:
            painter.FillRoundedRect(absolute_, card_radius_, theme.fill_input);
            painter.DrawInnerLight(absolute_, card_radius_, theme.edge_light,
                                   Color{0.0f, 0.0f, 0.0f, 0.55f});
            painter.StrokeRoundedRect(absolute_, card_radius_, theme.stroke_card);
            return;
        case CardStyle::Subtle:
            painter.FillRoundedRect(absolute_, card_radius_, theme.fill_hover);
            painter.StrokeRoundedRect(absolute_, card_radius_, theme.stroke_card);
            return;
        case CardStyle::Flyout:
        default:
            painter.FillRoundedRect(absolute_, card_radius_, theme.surface_flyout);
            painter.StrokeRoundedRect(absolute_, card_radius_, theme.stroke_card);
            return;
        }
    }
    if (card_fill_.a > 0.0f || card_stroke_.a > 0.0f) {
        painter.FillRoundedRect(absolute_, card_radius_, card_fill_);
        painter.StrokeRoundedRect(absolute_, card_radius_, card_stroke_);
        return;
    }
    if (background_.a > 0.0f) painter.FillRect(absolute_, background_);
    PaintShimmer(painter);
}

void Panel::Relayout() {
    RelayoutParent();
}

void Panel::AvoidControls(Painter& painter, const Theme& theme) {
    if (spotlight_t_ <= 0.004f) return;
    AvoidWalk(painter, theme, absolute_);
}

void Panel::AvoidWalk(Painter& painter, const Theme& theme, const Rect& clip) {
    // 追光只落在卡片面：递归找非命中穿透的叶子控件（按钮/输入等）垫回碳底，
    // 光被控件本身挡住；布局容器与 Label/IconView 视为卡片内容随光点亮。
    // 垫与沿途 ClipChildren 面板（表体/滚动视口）的裁剪区求交——虚拟化缓冲行
    // 的真实绘制已被裁掉，垫若不裁就会在表体外露出剪影。递归替代显式栈，绘制路径零堆。
    const Rect own = ClipChildren() ? clip.Intersect(ChildrenClipBounds()) : clip;
    for (const std::unique_ptr<Control>& child : children_) {
        if (!child->visible_) continue;
        if (auto* nested = child->AsPanel()) {
            nested->AvoidWalk(painter, theme, own);
            continue;
        }
        if (!child->BlocksCardSpotlight()) continue;
        const Rect pad = child->absolute_.Intersect(own);
        if (pad.IsEmpty()) continue;
        painter.FillRoundedRect(pad, child->ChromeRadius(theme), theme.fill_input);
    }
}

Panel& Panel::Shimmer(bool value) {
    if (shimmer_ == value) return *this;
    shimmer_ = value;
    if (shimmer_) Animate();
    Invalidate();
    return *this;
}

void Panel::PaintShimmer(Painter& painter) {
    if (!shimmer_) return;
    const float lit = std::max(spotlight_t_, hovered_ ? 1.0f : 0.0f);
    if (lit < 0.01f) return;
    const float band = 72.0f;
    const float span = absolute_.w + band * 2.0f;
    if (span < 1.0f) return;
    const float x = absolute_.x - band + std::fmod(shimmer_phase_ * 140.0f, span);
    painter.PushClip(absolute_);
    painter.FillRect({x, absolute_.y, band, absolute_.h}, Color{1.0f, 1.0f, 1.0f, 0.10f * lit});
    painter.PopClip();
}

void Panel::OnMouseEnter() {
    Control::OnMouseEnter();
    if (shimmer_) Animate();
}

void Panel::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_ || click_.Empty()) return;
    if (local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w && local.y <= absolute_.h) {
        click_.Emit();
    }
}

bool Panel::OnAnimate(float dt) {
    bool active = Control::OnAnimate(dt);
    if (shimmer_ && MotionScale() > 0.001f && (hovered_ || spotlight_inside_)) {
        shimmer_phase_ += dt;
        active = true;
    }
    return active;
}

} // namespace lumen

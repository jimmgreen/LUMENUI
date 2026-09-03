// input_router.cpp — WindowImpl 命中测试、捕获、悬停、聚光、焦点遍历。
#include "window_impl.h"
#include "hotkey.h"
#include "lumen/BusyOverlay.h"
#include "lumen/Dialog.h"
#include "lumen/Drawer.h"
#include "lumen/MenuBar.h"
#include "lumen/Panel.h"
#include "lumen/ScrollViewer.h"
#include "lumen/TitleBar.h"
#include "lumen/ToolTip.h"
#include <algorithm>
#include <vector>
#include <windows.h>

namespace lumen {

void WindowImpl::SetFocusControl(Control* control) {
    if (focused_ == control) return;
    if (focused_) {
        focused_->focused_ = false;
        focused_->OnFocusChanged(false);
        focused_->Animate();
    }
    focused_ = control;
    if (focused_) {
        focused_->focused_ = true;
        focused_->OnFocusChanged(true);
        focused_->Animate();
        EnsureFocusVisible();
    }
    SyncImeCaret();
    Invalidate();
    UiaOnFocus();
}

Control* WindowImpl::HitTree(Control* control, Point p) const {
    return HitTree(control, p, 0.0f);
}

Control* WindowImpl::HitTree(Control* control, Point p, float slop) const {
    if (!control || !control->visible_ || !control->enabled_) return nullptr;
    const Rect bounds = slop > 0.0f ? control->absolute_.Inset(-slop, -slop) : control->absolute_;
    if (!bounds.Contains(p)) return nullptr;
    if (control->CapturesOverlay(p)) {
        return control->HitTransparent() ? nullptr : control;
    }
    if (auto* panel = control->AsPanel()) {
        const Rect clip = slop > 0.0f ? panel->ChildrenClipBounds().Inset(-slop, -slop)
                                      : panel->ChildrenClipBounds();
        const bool clip_hits = panel->ClipChildren() && clip.Contains(p);
        if (!panel->ClipChildren() || clip_hits) {
            const Point q = panel->MapToChildren(p);
            for (size_t i = panel->ChildCount(); i-- > 0;) {
                if (Control* hit = HitTree(&panel->Child(i), q, slop)) return hit;
            }
        }
    }
    return control->HitTransparent() ? nullptr : control;
}

Control* WindowImpl::HitTest(Point p) {
    if (active_busy_) return HitTree(active_busy_, p, hit_slop_dip_);
    if (active_dialog_) return HitTree(active_dialog_, p, hit_slop_dip_);
    if (active_drawer_) return HitTree(active_drawer_, p, hit_slop_dip_);
    if (active_flyout_) return HitTree(active_flyout_, p, hit_slop_dip_);
    if (title_bar_ && p.y < CaptionHeight()) {
        if (Control* hit = HitTree(title_bar_.get(), p, hit_slop_dip_)) return hit;
    }
    return HitTree(root_.get(), p, hit_slop_dip_);
}

void WindowImpl::TrackMouse() {
    if (tracking_mouse_) return;
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&track);
    tracking_mouse_ = true;
}

void WindowImpl::OnMouseMove(int px, int py, uint32_t buttons) {
    Point p{static_cast<float>(px) / scale_, static_cast<float>(py) / scale_};
    const bool over_toast = UpdateToastHover(p);
    if (over_toast || toast_press_ >= 0) {
        if (!captured_ && hovered_) {
            hovered_->mouse_local_ = ToLocal(hovered_, p);
            hovered_->OnMouseLeave();
            hovered_ = nullptr;
        }
        TrackMouse();
        SyncSpotlights(p, true);
        HideTooltip(true);
        tooltip_control_ = nullptr;
        tooltip_last_pos_ = p;
        return;
    }
    const bool over_bubble =
        TooltipHit(p) && TooltipHasPayload() && (tooltip_shown_ || tooltip_fading_);
    if (!captured_) {
        if (over_bubble) {
            TrackMouse();
            Control* tip_hit = nullptr;
            if (tooltip_custom_ && tooltip_bubble_.Contains(p) &&
                (tooltip_close_.IsEmpty() || !tooltip_close_.Contains(p))) {
                tip_hit = HitTree(tooltip_custom_, p);
                if (tip_hit == tooltip_custom_) tip_hit = nullptr;
            }
            SetTooltipHover(tip_hit, p, buttons);
        } else {
            ClearTooltipHover();
            Control* hit = HitTest(p);
            if (hit != hovered_) {
                if (hovered_) {
                    hovered_->mouse_local_ = ToLocal(hovered_, p);
                    hovered_->OnMouseLeave();
                }
                hovered_ = hit;
                if (hovered_) {
                    hovered_->mouse_local_ = ToLocal(hovered_, p);
                    hovered_->OnMouseEnter();
                }
            }
            TrackMouse();
        }
    }
    Control* target = captured_ ? captured_ : hovered_;
    if (target && (captured_ || !over_bubble)) {
        target->mouse_local_ = ToLocal(target, p);
        target->OnMouseMove(ToLocal(target, p), buttons);
    }
    SyncSpotlights(p, true);
    // ToolTip：换目标或移动超 2px 都重置静置计时；指针在气泡/箭头走廊上保持显示。
    Control* tip_target =
        (!captured_ && hovered_ && hovered_->enabled_ && hovered_->HasToolTip()) ? hovered_
                                                                                : nullptr;
    if (tooltip_suppressed_ && tip_target != tooltip_suppressed_) {
        tooltip_suppressed_ = nullptr;
    }
    if (tip_target && tip_target == tooltip_suppressed_) tip_target = nullptr;
    if (over_bubble && !captured_) {
        // 气泡/箭头走廊上不换目标；滚轮引发的淡出继续走完。
    } else if (tip_target != tooltip_control_) {
        HideTooltip(true);
        tooltip_control_ = tip_target;
        tooltip_dwell_start_ = clock_seconds();
        if (tooltip_control_) RequestAnimation();
    } else if (tooltip_control_ && !tooltip_shown_) {
        const float dx = p.x - tooltip_last_pos_.x;
        const float dy = p.y - tooltip_last_pos_.y;
        if (dx * dx + dy * dy > 4.0f) {
            tooltip_dwell_start_ = clock_seconds();
            RequestAnimation();
        }
    }
    tooltip_last_pos_ = p;
}

void WindowImpl::SyncSpotlights(Point p, bool inside_window) {
    bool dirty = false;
    auto visit = [&](Control* tree) {
        if (!tree) return;
        std::vector<Control*> stack{tree};
        while (!stack.empty()) {
            Control* current = stack.back();
            stack.pop_back();
            if (!current || !current->visible_) continue;
            if (current->spotlight_enabled_) {
                if (inside_window) current->mouse_local_ = ToLocal(current, p);
                const bool inside = inside_window && current->absolute_.Contains(p);
                if (inside) current->spotlight_pos_ = current->mouse_local_;
                if (inside != current->spotlight_inside_) {
                    current->spotlight_inside_ = inside;
                    if (inside) {
                        current->spotlight_t_ = 1.0f;   // 进入当帧点亮，避免等 16ms 时钟
                    } else {
                        current->Animate();               // 离开才淡出
                    }
                    current->Invalidate();
                    dirty = true;
                } else if (inside) {
                    current->Invalidate();
                    dirty = true;
                }
            }
            if (auto* panel = current->AsPanel()) {
                for (size_t i = 0; i < panel->ChildCount(); ++i) stack.push_back(&panel->Child(i));
            }
        }
    };
    visit(title_bar_.get());
    visit(root_.get());
    if (active_drawer_) visit(active_drawer_);
    if (active_dialog_) visit(active_dialog_);
    if (active_busy_) visit(active_busy_);
    if (dirty) RequestPaint();
}

void WindowImpl::OnMouseButton(int px, int py, uint32_t buttons, bool down, uint32_t changed) {
    Point p{static_cast<float>(px) / scale_, static_cast<float>(py) / scale_};
    if (down) {
        keyboard_focus_ = false;
        if (!tooltip_close_.IsEmpty() && tooltip_close_.Contains(p)) {
            tooltip_suppressed_ = tooltip_control_;
            HideTooltip(true);
            tooltip_control_ = nullptr;
            tooltip_bubble_ = {};
            tooltip_close_ = {};
            return;
        }
        if (tooltip_shown_ && tooltip_custom_ && tooltip_bubble_.Contains(p)) {
            Control* hit = HitTree(tooltip_custom_, p);
            if (hit && hit != tooltip_custom_) {
                captured_ = hit;
                SetCapture(hwnd_);
                hit->OnMouseDown(WindowImpl::ToLocal(hit, p), buttons);
                return;
            }
            return;
        }
        HideTooltip(true);
        ptrdiff_t toast_i = -1;
        const ToastPart toast_part = HitToast(p, &toast_i);
        if (toast_part != ToastPart::None && changed == MK_LBUTTON) {
            toast_press_ = toast_i;
            toast_press_part_ = static_cast<int>(toast_part);
            SetCapture(hwnd_);
            return;
        }
        Control* hit = HitTest(p);
        if (!hit && active_busy_) return;
        if (!hit && active_dialog_) {
            if (active_dialog_->default_close_) active_dialog_->Dismiss(DialogResult::None);
            return;
        }
        if (!hit && active_drawer_) {
            RequestCloseDrawer();
            return;
        }
        if (!hit && active_flyout_) {
            CloseFlyout();   // 轻触关闭：弹层外的点击不再下漏到内容层
            return;
        }
        if (focused_ != hit) SetFocusControl(hit && hit->Focusable() ? hit : nullptr);
        if (hit) {
            if (hit != hovered_) {
                if (hovered_) {
                    hovered_->mouse_local_ = ToLocal(hovered_, p);
                    hovered_->OnMouseLeave();
                }
                hovered_ = hit;
                hovered_->mouse_local_ = ToLocal(hovered_, p);
                hovered_->OnMouseEnter();
            }
            captured_ = hit;
            SetCapture(hwnd_);
            hit->OnMouseDown(WindowImpl::ToLocal(hit, p), buttons);
        }
    } else {
        if (toast_press_ >= 0) {
            ptrdiff_t toast_i = -1;
            const ToastPart toast_part = HitToast(p, &toast_i);
            const ptrdiff_t pressed = toast_press_;
            const int press_part = toast_press_part_;
            toast_press_ = -1;
            toast_press_part_ = 0;
            ReleaseCapture();
            if (toast_i == pressed && static_cast<int>(toast_part) == press_part &&
                pressed >= 0 && static_cast<size_t>(pressed) < toasts_.size()) {
                Toast& toast = toasts_[static_cast<size_t>(pressed)];
                if (toast_part == ToastPart::Action) {
                    auto fn = toast.on_action;
                    BeginToastExit(toast, clock_seconds());
                    if (fn) fn();
                } else {
                    BeginToastExit(toast, clock_seconds());
                }
            }
            return;
        }
        Control* target = captured_;
        const bool tip_click =
            captured_ && tooltip_custom_ && OverlayContains(tooltip_custom_, captured_);
        if (captured_) {
            captured_ = nullptr;
            ReleaseCapture();
            target->OnMouseUp(WindowImpl::ToLocal(target, p), buttons);
        }
        if (tip_click) {
            HideTooltip(true);
            tooltip_control_ = nullptr;
        }
        if (changed == MK_RBUTTON && api_) {
            Control* leaf = target ? target : HitTest(p);
            for (Control* node = leaf; node; node = node->parent_) {
                if (node->ShowContextMenu(p)) break;
            }
        }
    }
}

bool WindowImpl::OnKeyDown(uint32_t vk) {
    keyboard_focus_ = true;
    if (vk == VK_ESCAPE && active_busy_) {
        active_busy_->OnKey(vk);
        return true;
    }
    if (vk == VK_ESCAPE && active_drawer_ && !active_dialog_) {
        active_drawer_->OnKey(vk);
        return true;
    }
    if (vk == VK_ESCAPE && active_flyout_ && !active_dialog_ && !active_busy_) {
        CloseFlyout();
        return true;
    }
    if (vk == VK_ESCAPE && active_dialog_) {
        active_dialog_->OnKey(vk);
        return true;
    }
    if (vk == VK_RETURN && active_dialog_) {
        active_dialog_->OnKey(vk);
        return true;
    }
    if (focused_ && focused_->OnKey(vk)) return true;
    if (TryShortcuts(vk)) return true;
    if (vk == VK_TAB) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        Control* next = nullptr;
        if (!focused_) {
            next = FindFirstFocusable();
        } else if (shift) {
            next = FindPrevFocusable(focused_);
        } else {
            next = FindNextFocusable(focused_);
        }
        if (next) SetFocusControl(next);
        return true;
    }
    return false;
}

bool WindowImpl::FireMenuShortcuts(const std::vector<MenuItem>& items, uint32_t vk) {
    for (const MenuItem& item : items) {
        if (item.separator || item.disabled) continue;
        if (!item.shortcut.empty() && item.action && ChordMatches(item.shortcut, vk)) {
            item.action();
            return true;
        }
        if (!item.children.empty() && FireMenuShortcuts(item.children, vk)) return true;
    }
    return false;
}

void WindowImpl::ScanMenuBarShortcuts(Control* tree, uint32_t vk, bool& hit) {
    if (hit || !tree || !tree->visible_ || !tree->enabled_) return;
    if (auto* bar = dynamic_cast<MenuBar*>(tree)) {
        for (Menu& menu : bar->menus_) {
            if (FireMenuShortcuts(menu.Items(), vk)) {
                hit = true;
                return;
            }
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0 && bar->ActivateMnemonic(vk)) {
            hit = true;
            return;
        }
    }
    if (auto* panel = tree->AsPanel()) {
        for (size_t i = 0; i < panel->ChildCount(); ++i) {
            ScanMenuBarShortcuts(&panel->Child(i), vk, hit);
            if (hit) return;
        }
    }
}

bool WindowImpl::TryShortcuts(uint32_t vk) {
    if (active_busy_ || active_dialog_) return false;
    const bool ime_inline = focused_ && focused_->ImeInline();
    auto chord_blocked = [&](std::wstring_view chord) {
        if (!ime_inline) return false;
        uint32_t want = 0;
        bool ctrl = false, shift = false, alt = false;
        if (!ParseChord(chord, want, ctrl, shift, alt)) return true;
        return !ctrl && !alt;
    };
    for (const Shortcut& slot : shortcuts_) {
        if (chord_blocked(slot.chord)) continue;
        if (ChordMatches(slot.chord, vk) && slot.fn) {
            slot.fn();
            return true;
        }
    }
    bool hit = false;
    if (title_bar_) ScanMenuBarShortcuts(title_bar_.get(), vk, hit);
    if (!hit) ScanMenuBarShortcuts(root_.get(), vk, hit);
    return hit;
}

void WindowImpl::CollectFocusable(Control* tree, std::vector<Control*>& order) {
    std::vector<Control*> stack{tree};
    while (!stack.empty()) {
        Control* node = stack.back();
        stack.pop_back();
        if (!node || !node->visible_ || !node->enabled_) continue;
        if (node->Focusable()) order.push_back(node);
        if (auto* panel = node->AsPanel()) {
            for (size_t i = panel->ChildCount(); i-- > 0;) stack.push_back(&panel->Child(i));
        }
    }
}

Control* WindowImpl::FindFirstFocusable() {
    std::vector<Control*> order;
    if (active_busy_) CollectFocusable(active_busy_, order);
    else if (active_dialog_) CollectFocusable(active_dialog_, order);
    else if (active_drawer_) CollectFocusable(active_drawer_, order);
    else {
        if (title_bar_) CollectFocusable(title_bar_.get(), order);
        CollectFocusable(root_.get(), order);
    }
    return order.empty() ? nullptr : order[0];
}

Control* WindowImpl::FindNextFocusable(Control* current) {
    std::vector<Control*> order;
    if (active_busy_) CollectFocusable(active_busy_, order);
    else if (active_dialog_) CollectFocusable(active_dialog_, order);
    else if (active_drawer_) CollectFocusable(active_drawer_, order);
    else {
        if (title_bar_) CollectFocusable(title_bar_.get(), order);
        CollectFocusable(root_.get(), order);
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == current) return order[(i + 1) % order.size()];
    }
    return order.empty() ? nullptr : order[0];
}

Control* WindowImpl::FindPrevFocusable(Control* current) {
    std::vector<Control*> order;
    if (active_busy_) CollectFocusable(active_busy_, order);
    else if (active_dialog_) CollectFocusable(active_dialog_, order);
    else if (active_drawer_) CollectFocusable(active_drawer_, order);
    else {
        if (title_bar_) CollectFocusable(title_bar_.get(), order);
        CollectFocusable(root_.get(), order);
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == current) {
            return order[(i + order.size() - 1) % order.size()];
        }
    }
    return order.empty() ? nullptr : order.back();
}

void WindowImpl::EnsureFocusVisible() {
    if (!focused_) return;
    for (Panel* p = focused_->parent_; p; p = p->parent_) {
        if (auto* sv = dynamic_cast<ScrollViewer*>(p)) {
            sv->EnsureVisible(focused_->AbsoluteBounds());
        }
    }
}

Point WindowImpl::ToLocal(const Control* control, Point absolute) {
    Point p = absolute;
    const Panel* chain[32];
    int depth = 0;
    for (const Panel* node = control->parent_; node && depth < 32; node = node->parent_) {
        chain[depth++] = node;
    }
    for (int i = depth - 1; i >= 0; --i) {
        p = chain[i]->MapToChildren(p);
    }
    return {p.x - control->absolute_.x, p.y - control->absolute_.y};
}

bool WindowImpl::LegacyMouseFromPointer() const {
    if (pointer_msg_time_ != 0 && GetMessageTime() == pointer_msg_time_) return true;
    const ULONG extra = static_cast<ULONG>(GetMessageExtraInfo());
    return (extra & 0xFFFFFF80u) == 0xFF515700u;
}

void WindowImpl::TryBeginPan(int px, int py) {
    if (!touch_input_ || panning_) return;
    if (captured_ && captured_->PrefersDragOverPan()) return;
    const float dx = static_cast<float>(px - pan_origin_px_.x);
    const float dy = static_cast<float>(py - pan_origin_px_.y);
    const float thresh = 8.0f * std::max(scale_, 0.01f);
    if (dx * dx + dy * dy < thresh * thresh) return;
    Control* start = captured_ ? captured_ : hovered_;
    Control* scroller = nullptr;
    for (Control* node = start; node; node = node->parent_) {
        if (node->CanPan()) {
            scroller = node;
            break;
        }
    }
    if (!scroller) return;
    if (captured_ && captured_ != scroller) {
        const Point dip{static_cast<float>(px) / scale_, static_cast<float>(py) / scale_};
        captured_->OnMouseUp(ToLocal(captured_, dip), 0);
        captured_->OnMouseLeave();
    }
    panning_ = true;
    pan_target_ = scroller;
    captured_ = scroller;
    hovered_ = scroller;
    pan_last_px_ = pan_origin_px_;
    SetCapture(hwnd_);
}

void WindowImpl::ApplyPanMove(int px, int py) {
    if (!pan_target_) return;
    const float dx = static_cast<float>(px - pan_last_px_.x) / scale_;
    const float dy = static_cast<float>(py - pan_last_px_.y) / scale_;
    pan_last_px_ = POINT{px, py};
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = 0.016f;
    if (qpc_freq_.QuadPart > 0 && pan_last_qpc_.QuadPart > 0) {
        dt = static_cast<float>(now.QuadPart - pan_last_qpc_.QuadPart) /
             static_cast<float>(qpc_freq_.QuadPart);
    }
    pan_last_qpc_ = now;
    if (dt > 0.0004f && dt < 0.08f) {
        pan_vx_ = pan_vx_ * 0.55f + (dx / dt) * 0.45f;
        pan_vy_ = pan_vy_ * 0.55f + (dy / dt) * 0.45f;
    }
    pan_target_->PanBy(dx, dy);
}

void WindowImpl::HandlePointerClient(int px, int py, DWORD type, int phase, uint32_t buttons,
                                     uint32_t changed) {
    touch_input_ = type == PT_TOUCH;
    hit_slop_dip_ = touch_input_ ? 8.0f : 0.0f;
    if (phase == 0) {
        pan_origin_px_ = POINT{px, py};
        pan_last_px_ = pan_origin_px_;
        QueryPerformanceCounter(&pan_last_qpc_);
        pan_vx_ = 0.0f;
        pan_vy_ = 0.0f;
        panning_ = false;
        pan_target_ = nullptr;
        OnMouseButton(px, py, buttons, true, changed ? changed : MK_LBUTTON);
        return;
    }
    if (phase == 1) {
        TryBeginPan(px, py);
        if (panning_ && pan_target_) ApplyPanMove(px, py);
        else OnMouseMove(px, py, buttons);
        return;
    }
    if (phase == 2) {
        if (panning_ && pan_target_) {
            pan_target_->PanFling(pan_vx_, pan_vy_);
            pan_target_ = nullptr;
            panning_ = false;
            if (captured_) {
                Control* target = captured_;
                captured_ = nullptr;
                ReleaseCapture();
                const Point dip{static_cast<float>(px) / scale_, static_cast<float>(py) / scale_};
                target->OnMouseUp(ToLocal(target, dip), 0);
            }
        } else {
            OnMouseButton(px, py, buttons, false, changed ? changed : MK_LBUTTON);
        }
        touch_input_ = false;
        hit_slop_dip_ = 0.0f;
        pointer_id_ = 0;
        return;
    }
    tracking_mouse_ = false;
    panning_ = false;
    pan_target_ = nullptr;
    touch_input_ = false;
    hit_slop_dip_ = 0.0f;
    pointer_id_ = 0;
    if (hovered_) {
        hovered_->OnMouseLeave();
        hovered_ = nullptr;
    }
    SyncSpotlights({-1.0f, -1.0f}, false);
}

bool WindowImpl::OnPointer(UINT msg, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    const UINT32 id = GET_POINTERID_WPARAM(wparam);
    if (msg == WM_POINTERWHEEL || msg == WM_POINTERHWHEEL) {
        POINTER_INFO info{};
        if (!GetPointerInfo(id, &info)) return false;
        pointer_msg_time_ = GetMessageTime();
        POINT client = info.ptPixelLocation;
        ScreenToClient(hwnd_, &client);
        HideTooltip();
        Point p{static_cast<float>(client.x) / scale_, static_cast<float>(client.y) / scale_};
        const float delta = static_cast<short>(HIWORD(wparam)) / static_cast<float>(WHEEL_DELTA);
        if (msg == WM_POINTERWHEEL) {
            for (Control* hit = HitTest(p); hit; hit = hit->parent_) {
                if (hit->OnWheel(delta)) break;
            }
        } else {
            for (Control* hit = HitTest(p); hit; hit = hit->parent_) {
                if (hit->OnHWheel(delta)) break;
            }
        }
        return true;
    }
    POINTER_INFO info{};
    if (!GetPointerInfo(id, &info)) return false;
    if (msg == WM_POINTERDOWN && pointer_id_ != 0 && pointer_id_ != id) return true;
    if ((msg == WM_POINTERUPDATE || msg == WM_POINTERUP || msg == WM_POINTERLEAVE ||
         msg == WM_POINTERCAPTURECHANGED) &&
        pointer_id_ != 0 && pointer_id_ != id) {
        return true;
    }
    pointer_msg_time_ = GetMessageTime();
    POINT client = info.ptPixelLocation;
    ScreenToClient(hwnd_, &client);
    uint32_t buttons = 0;
    if (info.pointerFlags & POINTER_FLAG_FIRSTBUTTON) buttons |= MK_LBUTTON;
    if (info.pointerFlags & POINTER_FLAG_SECONDBUTTON) buttons |= MK_RBUTTON;
    if (info.pointerFlags & POINTER_FLAG_THIRDBUTTON) buttons |= MK_MBUTTON;
    // POINTER_INFO 只有触点按钮；MK_SHIFT/CONTROL 仍要按键盘状态合成，
    // 否则 ListView 多选、文本框 Shift 拖选在 EnableMouseInPointer 下全失效。
    if (GetKeyState(VK_SHIFT) & 0x8000) buttons |= MK_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) buttons |= MK_CONTROL;
    // 抬起时 POINTER_FLAG_*BUTTON 往往已清；右键菜单必须看 ButtonChangeType。
    uint32_t changed = 0;
    switch (info.ButtonChangeType) {
    case POINTER_CHANGE_SECONDBUTTON_DOWN:
    case POINTER_CHANGE_SECONDBUTTON_UP:
        changed = MK_RBUTTON;
        buttons |= MK_RBUTTON;
        break;
    case POINTER_CHANGE_THIRDBUTTON_DOWN:
    case POINTER_CHANGE_THIRDBUTTON_UP:
        changed = MK_MBUTTON;
        buttons |= MK_MBUTTON;
        break;
    case POINTER_CHANGE_FIRSTBUTTON_DOWN:
    case POINTER_CHANGE_FIRSTBUTTON_UP:
        changed = MK_LBUTTON;
        buttons |= MK_LBUTTON;
        break;
    default:
        break;
    }
    if (msg == WM_POINTERDOWN) {
        pointer_id_ = id;
        HandlePointerClient(client.x, client.y, info.pointerType, 0, buttons, changed);
        return true;
    }
    if (msg == WM_POINTERUPDATE) {
        HandlePointerClient(client.x, client.y, info.pointerType, 1, buttons, changed);
        return true;
    }
    if (msg == WM_POINTERUP || msg == WM_POINTERCAPTURECHANGED) {
        HandlePointerClient(client.x, client.y, info.pointerType, 2, buttons, changed);
        return true;
    }
    if (msg == WM_POINTERLEAVE) {
        HandlePointerClient(client.x, client.y, info.pointerType, 3, buttons, changed);
        return true;
    }
    return false;
}

void WindowImpl::DispatchTouch(Point client_dip, int phase) {
    HandlePointerClient(static_cast<int>(client_dip.x * scale_ + 0.5f),
                        static_cast<int>(client_dip.y * scale_ + 0.5f), PT_TOUCH, phase,
                        MK_LBUTTON, MK_LBUTTON);
}

} // namespace lumen

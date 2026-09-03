#include "lumen/NavigationView.h"
#include "lumen/AutoSuggestBox.h"
#include "lumen/Breadcrumb.h"
#include "lumen/Icons.h"
#include "lumen/PageHost.h"
#include "lumen/Painter.h"
#include "lumen/ScrollViewer.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lumen {
namespace {
constexpr float kAutoThreshold = 720.0f;
constexpr float kRowHeight = 40.0f;
constexpr float kHeaderHeight = 28.0f;
constexpr float kPanePad = 8.0f;
constexpr float kIndent = 18.0f;
constexpr float kBarHit = 10.0f;
constexpr float kSearchGap = 8.0f;

bool ContainsInsensitive(std::wstring_view hay, std::wstring_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    const auto fold = [](wchar_t ch) noexcept {
        return static_cast<wchar_t>(towlower(static_cast<unsigned short>(ch)));
    };
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (fold(hay[i + j]) != fold(needle[j])) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}
}

struct NavigationView::Impl {
    struct Row { bool footer = false; size_t index = 0; int depth = 0; Rect rect; };

    class ChildPopup : public Control {
    public:
        explicit ChildPopup(Impl* owner) : owner_(owner) {}
        void Rebuild(std::wstring_view root) {
            entries_.clear();
            root_ = root;
            Append(root, 0);
            focus_ = entries_.empty() ? -1 : 0;
        }
    protected:
        Size Measure(Size, const Theme&) override {
            return {260.0f, std::max(48.0f, 8.0f + entries_.size() * 36.0f)};
        }
        bool Focusable() const noexcept override { return true; }
        CursorShape CursorAt(Point local) const override {
            const int row = static_cast<int>((local.y - 4.0f) / 36.0f);
            if (row < 0 || row >= static_cast<int>(entries_.size())) return CursorShape::Arrow;
            const NavigationItem& item = owner_->items[entries_[static_cast<size_t>(row)].first];
            return item.enabled ? CursorShape::Hand : CursorShape::Arrow;
        }
        void Draw(Painter& painter, const Theme& theme) override {
            painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.bg);
            painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.fill_input);
            painter.StrokeRoundedRect(absolute_, theme.radius_flyout, theme.stroke_card);
            for (size_t row = 0; row < entries_.size(); ++row) {
                const NavigationItem& item = owner_->items[entries_[row].first];
                const Rect slot{absolute_.x + 4.0f, absolute_.y + 4.0f + row * 36.0f,
                                absolute_.w - 8.0f, 36.0f};
                if (static_cast<int>(row) == focus_) painter.FillRoundedRect(slot, 7.0f, theme.fill_hover);
                const float x = slot.x + 10.0f + entries_[row].second * kIndent;
                painter.DrawIcon(item.glyph.empty() ? icon::kLayers : item.glyph,
                                 {x, slot.y, 22.0f, slot.h}, 15.0f,
                                 item.enabled ? theme.text : theme.text_disabled);
                const bool has_chevron = owner_->HasChildren(entries_[row].first);
                float text_trail = has_chevron ? 36.0f : 16.0f;
                if (!item.badge.Empty()) text_trail += MeasureInfoBadge(item.badge).w + 6.0f;
                painter.DrawText(item.text, {x + 28.0f, slot.y, slot.Right() - x - text_trail, slot.h},
                                 TextRole::Body, item.enabled ? theme.text : theme.text_disabled);
                if (!item.badge.Empty()) {
                    const Size sz = MeasureInfoBadge(item.badge);
                    const float chevron = has_chevron ? 18.0f : 0.0f;
                    PaintInfoBadge(painter, theme,
                                   {slot.Right() - 8.0f - chevron - sz.w * 0.5f,
                                    slot.y + slot.h * 0.5f},
                                   item.badge);
                }
                if (has_chevron) {
                    painter.DrawChevron({slot.Right() - 16.0f, slot.y + slot.h * 0.5f}, 9.0f,
                                        owner_->expanded.count(item.id) ? 0.0f : -90.0f,
                                        theme.text_secondary, 1.4f);
                }
            }
        }
        bool OnKey(uint32_t vk) override {
            if (vk == VK_ESCAPE) { WindowImpl::CloseTransient(window_); return true; }
            if (vk == VK_UP || vk == VK_DOWN || vk == VK_HOME || vk == VK_END) {
                if (entries_.empty()) return true;
                if (vk == VK_HOME) focus_ = 0;
                else if (vk == VK_END) focus_ = static_cast<int>(entries_.size()) - 1;
                else focus_ = Clamp(focus_ + (vk == VK_DOWN ? 1 : -1), 0,
                                    static_cast<int>(entries_.size()) - 1);
                Invalidate();
                return true;
            }
            if (vk == VK_RETURN || vk == VK_SPACE) { Invoke(focus_); return true; }
            return false;
        }
        void OnMouseMove(Point local, uint32_t) override {
            const int row = static_cast<int>((local.y - 4.0f) / 36.0f);
            const int next = row >= 0 && row < static_cast<int>(entries_.size()) ? row : -1;
            if (next != focus_) { focus_ = next; Invalidate(); }
        }
        void OnMouseDown(Point local, uint32_t buttons) override {
            if (!(buttons & MK_LBUTTON)) return;
            Focus();
            Invoke(static_cast<int>((local.y - 4.0f) / 36.0f));
        }
    private:
        void Append(std::wstring_view parent, int depth) {
            for (size_t i = 0; i < owner_->items.size(); ++i) {
                if (owner_->EffectiveParent(i) != parent || owner_->items[i].type != NavigationItemType::Item) continue;
                entries_.push_back({i, depth});
                if (owner_->expanded.count(owner_->items[i].id)) Append(owner_->items[i].id, depth + 1);
            }
        }
        void Invoke(int row) {
            if (row < 0 || row >= static_cast<int>(entries_.size())) return;
            const size_t index = entries_[static_cast<size_t>(row)].first;
            if (!owner_->items[index].enabled) return;
            owner_->SelectItem(index, true);
            WindowImpl::CloseTransient(window_);
        }
        Impl* owner_ = nullptr;
        std::wstring root_;
        std::vector<std::pair<size_t, int>> entries_;
        int focus_ = -1;
    };

    explicit Impl(NavigationView* control) : owner(control), popup(this) {
        content = &owner->Add<StackPanel>();
    }

    bool Compact(float width) const {
        if (force_expanded) return false;
        return manually_compact || mode == NavigationDisplayMode::Compact ||
               (mode == NavigationDisplayMode::Auto && width < kAutoThreshold);
    }
    void ExpandPane() {
        force_expanded = true;
        manually_compact = false;
    }
    void TogglePane(float width) {
        if (Compact(width)) ExpandPane();
        else {
            force_expanded = false;
            manually_compact = true;
        }
    }
    float Width(float outer) const { return Compact(outer) ? compact_length : pane_length; }

    std::wstring_view EffectiveParent(size_t index) const {
        const NavigationItem& item = items[index];
        if (item.parent_id.empty() || item.parent_id == item.id) return {};
        const auto found = by_id.find(item.parent_id);
        if (found == by_id.end()) return {};
        std::unordered_set<std::wstring> seen;
        std::wstring_view cursor = item.parent_id;
        while (!cursor.empty()) {
            if (!seen.emplace(cursor).second || cursor == item.id) return {};
            const auto it = by_id.find(std::wstring(cursor));
            if (it == by_id.end()) break;
            cursor = items[it->second].parent_id;
        }
        return item.parent_id;
    }

    bool HasChildren(size_t index) const {
        if (index >= items.size()) return false;
        for (size_t i = 0; i < items.size(); ++i) if (EffectiveParent(i) == items[index].id) return true;
        return false;
    }

    bool ItemMatches(size_t index) const {
        if (index >= items.size()) return false;
        const NavigationItem& item = items[index];
        if (item.type != NavigationItemType::Item) return false;
        return ContainsInsensitive(item.text, query);
    }

    bool LineageMatches(size_t index) const {
        if (ItemMatches(index)) return true;
        if (index >= items.size()) return false;
        const std::wstring& id = items[index].id;
        for (size_t i = 0; i < items.size(); ++i) {
            if (EffectiveParent(i) == id && LineageMatches(i)) return true;
        }
        return false;
    }

    bool Filtering() const noexcept { return !query.empty(); }

    float SearchBand() const noexcept {
        return search_enabled ? kSearchGap + kRowHeight + kSearchGap : 0.0f;
    }

    void RebuildIndex() {
        by_id.clear();
        expanded.clear();
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].id.empty() && !by_id.count(items[i].id)) by_id.emplace(items[i].id, i);
            if (items[i].expanded) expanded.insert(items[i].id);
        }
    }

    void AppendVisible(std::wstring_view parent, int depth, float& y, float width) {
        for (size_t i = 0; i < items.size(); ++i) {
            const NavigationItem& item = items[i];
            if (EffectiveParent(i) != parent) continue;
            if (Filtering()) {
                if (item.type != NavigationItemType::Item || !LineageMatches(i)) continue;
            }
            const float h = item.type == NavigationItemType::Header ? kHeaderHeight
                          : item.type == NavigationItemType::Separator ? 12.0f : kRowHeight;
            rows.push_back({false, i, depth,
                            {kPanePad, y, std::max(0.0f, width - kPanePad * 2.0f), h}});
            y += h;
            if (item.type == NavigationItemType::Item &&
                (expanded.count(item.id) || (Filtering() && LineageMatches(i)))) {
                AppendVisible(item.id, depth + 1, y, width);
            }
        }
    }

    void BuildRows(float height, float width) {
        rows.clear();
        main_top = kPanePad + kRowHeight + SearchBand();
        float y = main_top;
        AppendVisible({}, 0, y, width);
        float footer_y = height - kPanePad;
        for (auto it = footer_items.rbegin(); it != footer_items.rend(); ++it) {
            const float h = it->type == NavigationItemType::Header ? kHeaderHeight
                          : it->type == NavigationItemType::Separator ? 12.0f : kRowHeight;
            footer_y -= h;
        }
        footer_top = footer_y;
        const float main_view_height = std::max(0.0f, footer_top - main_top);
        main_content_height = std::max(0.0f, y - main_top + kPanePad);
        max_scroll = std::max(0.0f, main_content_height - main_view_height);
        scroll_y = Clamp(scroll_y, 0.0f, max_scroll);
        for (Row& row : rows) row.rect.y -= scroll_y;
        size_t index = 0;
        for (const NavigationItem& item : footer_items) {
            const float h = item.type == NavigationItemType::Header ? kHeaderHeight
                          : item.type == NavigationItemType::Separator ? 12.0f : kRowHeight;
            rows.push_back({true, index++, 0,
                            {kPanePad, footer_y, std::max(0.0f, width - kPanePad * 2.0f), h}});
            footer_y += h;
        }
        for (size_t row = 0; row < rows.size(); ++row) {
            if (ItemAt(rows[row]).id == selected) {
                focus = static_cast<int>(row);
                break;
            }
        }
        if (focus >= static_cast<int>(rows.size())) focus = FirstSelectable(0, 1);
    }

    const NavigationItem& ItemAt(const Row& row) const {
        return (row.footer ? footer_items : items)[row.index];
    }
    int FirstSelectable(int start, int direction) const {
        for (int i = start; i >= 0 && i < static_cast<int>(rows.size()); i += direction) {
            const NavigationItem& item = ItemAt(rows[static_cast<size_t>(i)]);
            if (item.type == NavigationItemType::Item && item.enabled) return i;
        }
        return -1;
    }
    int Hit(Point local) const {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].footer && rows[i].rect.Contains(local)) return static_cast<int>(i);
        }
        if (local.y < main_top || local.y >= footer_top) return -1;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].footer && rows[i].rect.Contains(local)) return static_cast<int>(i);
        }
        return -1;
    }
    // -2 折叠按钮，-3 紧凑搜索，>=0 行，-1 窗格空白。
    int HitPane(Point local, float outer_w) const {
        if (local.x >= Width(outer_w)) return -1;
        if (local.y >= kPanePad && local.y < kPanePad + kRowHeight) return -2;
        if (search_enabled && Compact(outer_w)) {
            const float top = kPanePad + kRowHeight + kSearchGap;
            if (local.y >= top && local.y < top + kRowHeight) return -3;
        }
        return Hit(local);
    }
    bool PaneTargetClickable(int hit) const {
        if (hit == -2 || hit == -3) return true;
        if (hit < 0 || hit >= static_cast<int>(rows.size())) return false;
        const NavigationItem& item = ItemAt(rows[static_cast<size_t>(hit)]);
        return item.type == NavigationItemType::Item && item.enabled;
    }
    void SelectItem(size_t index, bool notify) {
        if (index >= items.size() || items[index].type != NavigationItemType::Item || !items[index].enabled) return;
        if (items[index].action) {
            if (notify) invoked.Emit(items[index].id);
            return;
        }
        selected = items[index].id;
        for (size_t row = 0; row < rows.size(); ++row) {
            if (!rows[row].footer && rows[row].index == index) focus = static_cast<int>(row);
        }
        SyncPath();
        if (notify) {
            changed.Emit(selected);
            ApplyBoundPage();
        }
        owner->Invalidate();
    }
    void Select(int row, bool notify) {
        if (row < 0 || row >= static_cast<int>(rows.size())) return;
        const Row& r = rows[static_cast<size_t>(row)];
        if (r.footer) {
            const NavigationItem& item = footer_items[r.index];
            if (item.type != NavigationItemType::Item || !item.enabled) return;
            if (item.action) {
                if (notify) invoked.Emit(item.id);
                return;
            }
            selected = item.id;
            focus = row;
            SyncPath();
            if (notify) {
                changed.Emit(selected);
                ApplyBoundPage();
            }
            owner->Invalidate();
        } else SelectItem(r.index, notify);
    }

    void SelectById(std::wstring_view id, bool notify) {
        const auto found = by_id.find(std::wstring(id));
        if (found != by_id.end()) {
            SelectItem(found->second, notify);
            return;
        }
        for (const NavigationItem& item : footer_items) {
            if (item.id == id && item.type == NavigationItemType::Item && item.enabled) {
                if (item.action) {
                    if (notify) invoked.Emit(item.id);
                    return;
                }
                selected.assign(id.begin(), id.end());
                SyncPath();
                if (notify) {
                    changed.Emit(selected);
                    ApplyBoundPage();
                }
                owner->Invalidate();
                return;
            }
        }
    }

    void RebuildPath() {
        path_ids.clear();
        path_titles.clear();
        if (selected.empty()) return;
        const auto found = by_id.find(selected);
        if (found != by_id.end()) {
            size_t index = found->second;
            std::unordered_set<std::wstring> seen;
            while (index < items.size() && seen.insert(items[index].id).second) {
                path_ids.insert(path_ids.begin(), items[index].id);
                path_titles.insert(path_titles.begin(), items[index].text);
                const std::wstring& parent = items[index].parent_id;
                if (parent.empty()) break;
                const auto it = by_id.find(parent);
                if (it == by_id.end()) break;
                index = it->second;
            }
            return;
        }
        for (const NavigationItem& item : footer_items) {
            if (item.id == selected) {
                path_ids.push_back(item.id);
                path_titles.push_back(item.text);
                return;
            }
        }
    }

    void FillCrumb(Breadcrumb* target) {
        if (!target) return;
        target->Items(path_titles);
    }

    void SyncPath() {
        RebuildPath();
        FillCrumb(crumb);
        FillCrumb(bound_crumb);
    }

    void WireCrumb(Breadcrumb* target) {
        if (!target) return;
        target->OnNavigate([this](size_t index) {
            if (index < path_ids.size()) SelectById(path_ids[index], true);
        });
    }

    int IndexOf(Control* control) const {
        if (!control) return -1;
        for (size_t i = 0; i < owner->ChildCount(); ++i) {
            if (&owner->Child(i) == control) return static_cast<int>(i);
        }
        return -1;
    }

    void EnsureSearch() {
        if (search) return;
        search = &owner->Add<AutoSuggestBox>();
        search->Placeholder(search_placeholder)
            .Glyph(icon::kSearch)
            .MaxSuggestions(8)
            .Suggestions([this](std::wstring_view q) { return Suggest(q); })
            .OnSuggestionChosen([this](std::wstring_view title) { PickSuggestion(title); })
            .OnTextChanged([this](std::wstring_view) { ApplyQuery(search->Text()); });
        search->Visible(false);
    }

    void EnsureCrumb() {
        if (crumb) return;
        crumb = &owner->Add<Breadcrumb>();
        WireCrumb(crumb);
        crumb->Visible(false);
        SyncPath();
    }

    std::vector<std::wstring> Suggest(std::wstring_view q) const {
        std::vector<std::wstring> out;
        if (q.empty()) return out;
        auto take = [&](const NavigationItem& item) {
            if (item.type != NavigationItemType::Item || !item.enabled) return;
            if (!ContainsInsensitive(item.text, q)) return;
            out.push_back(item.text);
        };
        for (const NavigationItem& item : items) take(item);
        for (const NavigationItem& item : footer_items) take(item);
        return out;
    }

    void PickSuggestion(std::wstring_view title) {
        auto pick = [&](const NavigationItem& item, bool main) {
            if (item.type != NavigationItemType::Item || item.text != title) return false;
            query.clear();
            if (search) search->Text(L"");
            if (main) {
                owner->RevealId(item.id);
                SelectById(item.id, true);
            } else {
                SelectById(item.id, true);
            }
            return true;
        };
        for (const NavigationItem& item : items) {
            if (pick(item, true)) return;
        }
        for (const NavigationItem& item : footer_items) {
            if (pick(item, false)) return;
        }
    }

    void ApplyQuery(std::wstring_view q) {
        query.assign(q.begin(), q.end());
        const Rect bounds = owner->AbsoluteBounds();
        if (bounds.w > 0.0f && bounds.h > 0.0f) {
            BuildRows(bounds.h, Width(bounds.w));
        }
        owner->Invalidate();
        search_changed.Emit(query);
    }

    void ApplyBoundPage() {
        if (!page_host || selected.empty()) return;
        page_host->Show(selected);
        if (Control* parent = page_host->Parent()) {
            if (auto* scroll = dynamic_cast<ScrollViewer*>(parent)) scroll->ScrollToY(0.0f);
        }
    }

    NavigationView* owner = nullptr;
    StackPanel* content = nullptr;
    AutoSuggestBox* search = nullptr;
    Breadcrumb* crumb = nullptr;
    Breadcrumb* bound_crumb = nullptr;
    PageHost* page_host = nullptr;
    std::vector<NavigationItem> items;
    std::vector<NavigationItem> footer_items;
    std::vector<Row> rows;
    std::unordered_map<std::wstring, size_t> by_id;
    std::unordered_set<std::wstring> expanded;
    std::wstring selected;
    std::wstring query;
    std::wstring search_placeholder = L"Search";
    std::vector<std::wstring> path_ids;
    std::vector<std::wstring> path_titles;
    Signal<std::wstring_view> changed;
    Signal<std::wstring_view> invoked;
    Signal<std::wstring_view, bool> expanded_changed;
    Signal<std::wstring_view> search_changed;
    NavigationDisplayMode mode = NavigationDisplayMode::Auto;
    float pane_length = 220.0f;
    float compact_length = 52.0f;
    float preferred_height = 0.0f;
    float scroll_y = 0.0f;
    float max_scroll = 0.0f;
    float main_content_height = 0.0f;
    float main_top = 0.0f;
    float footer_top = 0.0f;
    bool manually_compact = false;
    bool force_expanded = false;
    bool search_enabled = false;
    bool crumb_visible = false;
    int hover = -1;
    int focus = -1;
    ChildPopup popup;
};

NavigationView::NavigationView() : impl_(std::make_unique<Impl>(this)) {}
NavigationView::~NavigationView() {
    if (window_ && WindowImpl::TransientActive(window_, &impl_->popup)) WindowImpl::CloseTransient(window_);
}

NavigationView& NavigationView::Items(std::vector<NavigationItem> items) {
    impl_->items = std::move(items);
    impl_->RebuildIndex();
    impl_->SyncPath();
    Relayout();
    return *this;
}
NavigationView& NavigationView::FooterItems(std::vector<NavigationItem> items) {
    impl_->footer_items = std::move(items);
    impl_->SyncPath();
    Relayout();
    return *this;
}
const std::wstring& NavigationView::SelectedId() const noexcept { return impl_->selected; }

AutomationControlType NavigationView::AutomationType() const noexcept {
    return AutomationControlType::Tree;
}
uint32_t NavigationView::AutomationPatterns() const noexcept { return kPatternSelection; }

int NavigationView::AutomationItemCount() const noexcept {
    int n = 0;
    for (const NavigationItem& item : impl_->items) {
        if (item.type == NavigationItemType::Item) ++n;
    }
    for (const NavigationItem& item : impl_->footer_items) {
        if (item.type == NavigationItemType::Item) ++n;
    }
    return n;
}

int NavigationView::AutomationSelectedIndex() const noexcept {
    int i = 0;
    for (const NavigationItem& item : impl_->items) {
        if (item.type != NavigationItemType::Item) continue;
        if (item.id == impl_->selected) return i;
        ++i;
    }
    for (const NavigationItem& item : impl_->footer_items) {
        if (item.type != NavigationItemType::Item) continue;
        if (item.id == impl_->selected) return i;
        ++i;
    }
    return -1;
}

bool NavigationView::AutomationSelectIndex(int index) {
    if (index < 0) return false;
    int i = 0;
    for (const NavigationItem& item : impl_->items) {
        if (item.type != NavigationItemType::Item) continue;
        if (i == index) {
            SelectedId(item.id);
            return true;
        }
        ++i;
    }
    for (const NavigationItem& item : impl_->footer_items) {
        if (item.type != NavigationItemType::Item) continue;
        if (i == index) {
            SelectedId(item.id);
            return true;
        }
        ++i;
    }
    return false;
}

std::wstring NavigationView::AutomationItemName(int index) const {
    if (index < 0) return {};
    int i = 0;
    for (const NavigationItem& item : impl_->items) {
        if (item.type != NavigationItemType::Item) continue;
        if (i == index) return item.text;
        ++i;
    }
    for (const NavigationItem& item : impl_->footer_items) {
        if (item.type != NavigationItemType::Item) continue;
        if (i == index) return item.text;
        ++i;
    }
    return {};
}

NavigationView& NavigationView::SelectedId(std::wstring_view id) {
    impl_->SelectById(id, false);
    return *this;
}

NavigationView& NavigationView::ItemExpanded(std::wstring_view id, bool expanded) {
    const auto found = impl_->by_id.find(std::wstring(id));
    if (found == impl_->by_id.end() || !impl_->HasChildren(found->second)) return *this;
    const bool old = impl_->expanded.count(std::wstring(id)) != 0;
    if (old == expanded) return *this;
    if (expanded) impl_->expanded.insert(std::wstring(id)); else impl_->expanded.erase(std::wstring(id));
    Relayout();
    impl_->expanded_changed.Emit(id, expanded);
    return *this;
}

bool NavigationView::ItemExpanded(std::wstring_view id) const {
    return impl_->expanded.count(std::wstring(id)) != 0;
}

NavigationView& NavigationView::ItemBadge(std::wstring_view id, InfoBadgeData badge) {
    for (NavigationItem& item : impl_->items) {
        if (item.id == id) {
            item.badge = std::move(badge);
            Invalidate();
            return *this;
        }
    }
    for (NavigationItem& item : impl_->footer_items) {
        if (item.id == id) {
            item.badge = std::move(badge);
            Invalidate();
            return *this;
        }
    }
    return *this;
}

InfoBadgeData NavigationView::ItemBadge(std::wstring_view id) const {
    for (const NavigationItem& item : impl_->items) {
        if (item.id == id) return item.badge;
    }
    for (const NavigationItem& item : impl_->footer_items) {
        if (item.id == id) return item.badge;
    }
    return {};
}

NavigationView& NavigationView::RevealId(std::wstring_view id) {
    const auto found = impl_->by_id.find(std::wstring(id));
    if (found == impl_->by_id.end()) return *this;
    std::wstring parent = impl_->items[found->second].parent_id;
    std::unordered_set<std::wstring> seen;
    while (!parent.empty() && seen.insert(parent).second) {
        impl_->expanded.insert(parent);
        const auto it = impl_->by_id.find(parent);
        if (it == impl_->by_id.end()) break;
        parent = impl_->items[it->second].parent_id;
    }
    Relayout();
    return SelectedId(id);
}

NavigationView& NavigationView::OnSelectionChanged(std::function<void(std::wstring_view)> handler) {
    impl_->changed.Subscribe(std::move(handler));
    return *this;
}
Connection NavigationView::BindSelectionChanged(std::function<void(std::wstring_view)> handler) {
    return impl_->changed.Connect(std::move(handler));
}
NavigationView& NavigationView::OnItemInvoked(std::function<void(std::wstring_view)> handler) {
    impl_->invoked.Subscribe(std::move(handler));
    return *this;
}
Connection NavigationView::BindItemInvoked(std::function<void(std::wstring_view)> handler) {
    return impl_->invoked.Connect(std::move(handler));
}
NavigationView& NavigationView::BindPages(PageHost& host) {
    impl_->page_host = &host;
    impl_->ApplyBoundPage();
    return *this;
}
StackPanel& NavigationView::Page(std::wstring_view id) {
    if (!impl_->page_host) {
        Control::DebugTrap(L"LUMEN_CHECK: BindPages before Page()");
        return *impl_->content;
    }
    return impl_->page_host->Page(id);
}
NavigationView& NavigationView::Navigate(std::wstring_view id) {
    impl_->SelectById(id, true);
    return *this;
}
NavigationView& NavigationView::OnExpandedChanged(std::function<void(std::wstring_view, bool)> handler) {
    impl_->expanded_changed.Subscribe(std::move(handler));
    return *this;
}
Connection NavigationView::BindExpandedChanged(std::function<void(std::wstring_view, bool)> handler) {
    return impl_->expanded_changed.Connect(std::move(handler));
}
NavigationView& NavigationView::DisplayMode(NavigationDisplayMode value) {
    impl_->mode = value;
    impl_->manually_compact = false;
    impl_->force_expanded = false;
    Relayout();
    return *this;
}
NavigationDisplayMode NavigationView::DisplayMode() const noexcept { return impl_->mode; }
NavigationView& NavigationView::PaneLength(float value) { impl_->pane_length = std::max(120.0f, value); Relayout(); return *this; }
NavigationView& NavigationView::CompactLength(float value) { impl_->compact_length = std::max(40.0f, value); Relayout(); return *this; }
NavigationView& NavigationView::Height(float value) { impl_->preferred_height = std::max(0.0f, value); Relayout(); return *this; }
StackPanel& NavigationView::Content() { return *impl_->content; }

NavigationView& NavigationView::SearchEnabled(bool on) {
    impl_->search_enabled = on;
    if (on) impl_->EnsureSearch();
    Relayout();
    return *this;
}
bool NavigationView::SearchEnabled() const noexcept { return impl_->search_enabled; }

NavigationView& NavigationView::SearchPlaceholder(std::wstring_view text) {
    impl_->search_placeholder = std::wstring(text);
    if (impl_->search) impl_->search->Placeholder(impl_->search_placeholder);
    return *this;
}

NavigationView& NavigationView::SearchQuery(std::wstring_view query) {
    if (impl_->search_enabled) impl_->EnsureSearch();
    if (impl_->search) impl_->search->Text(query);
    impl_->ApplyQuery(query);
    return *this;
}

const std::wstring& NavigationView::SearchQuery() const noexcept { return impl_->query; }

NavigationView& NavigationView::OnSearch(std::function<void(std::wstring_view)> handler) {
    impl_->search_changed.Subscribe(std::move(handler));
    return *this;
}
Connection NavigationView::BindSearch(std::function<void(std::wstring_view)> handler) {
    return impl_->search_changed.Connect(std::move(handler));
}

AutoSuggestBox* NavigationView::SearchBox() noexcept { return impl_->search; }
const AutoSuggestBox* NavigationView::SearchBox() const noexcept { return impl_->search; }

NavigationView& NavigationView::ShowBreadcrumb(bool on) {
    impl_->crumb_visible = on;
    if (on) impl_->EnsureCrumb();
    Relayout();
    return *this;
}
bool NavigationView::BreadcrumbVisible() const noexcept { return impl_->crumb_visible; }

std::vector<std::wstring> NavigationView::PathIds() const { return impl_->path_ids; }
std::vector<std::wstring> NavigationView::PathTitles() const { return impl_->path_titles; }

NavigationView& NavigationView::BindBreadcrumb(Breadcrumb& crumb) {
    impl_->bound_crumb = &crumb;
    impl_->WireCrumb(&crumb);
    impl_->SyncPath();
    return *this;
}

Size NavigationView::Measure(Size available, const Theme& theme) {
    const auto finite = [](float value) { return value >= 0.0f && value < 1.0e4f; };
    const float width = finite(available.w) ? available.w : 640.0f;
    const float natural_height = impl_->preferred_height > 0.0f ? impl_->preferred_height : 420.0f;
    const float height = finite(available.h) ? (impl_->preferred_height > 0.0f ? std::min(impl_->preferred_height, available.h) : available.h) : natural_height;
    const float pane = impl_->Width(width);
    const float crumb_h = (impl_->crumb && impl_->crumb_visible) ? theme.input_height : 0.0f;
    const int content_i = impl_->IndexOf(impl_->content);
    if (content_i >= 0) {
        MeasureChildAt(static_cast<size_t>(content_i),
                       {std::max(0.0f, width - pane), std::max(0.0f, height - crumb_h)}, theme);
    }
    if (impl_->search) {
        const int i = impl_->IndexOf(impl_->search);
        if (i >= 0) {
            MeasureChildAt(static_cast<size_t>(i),
                           {std::max(0.0f, pane - kPanePad * 2.0f), theme.input_height}, theme);
        }
    }
    if (impl_->crumb) {
        const int i = impl_->IndexOf(impl_->crumb);
        if (i >= 0) {
            MeasureChildAt(static_cast<size_t>(i), {std::max(0.0f, width - pane), theme.input_height},
                           theme);
        }
    }
    return {width, height};
}

void NavigationView::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const float pane = impl_->Width(absolute.w);
    const bool compact = impl_->Compact(absolute.w);
    const float crumb_h =
        (impl_->crumb && impl_->crumb_visible) ? std::max(impl_->crumb->DesiredSize().h, 32.0f) : 0.0f;
    if (impl_->search) {
        impl_->search->Visible(impl_->search_enabled && !compact);
        const int i = impl_->IndexOf(impl_->search);
        if (i >= 0 && impl_->search->Visible()) {
            SetChildBounds(*impl_->search,
                           {kPanePad, kPanePad + kRowHeight + kSearchGap,
                            std::max(0.0f, pane - kPanePad * 2.0f), kRowHeight});
            ArrangeChildAt(static_cast<size_t>(i));
        }
    }
    if (impl_->crumb) {
        impl_->crumb->Visible(impl_->crumb_visible);
        const int i = impl_->IndexOf(impl_->crumb);
        if (i >= 0 && impl_->crumb->Visible()) {
            SetChildBounds(*impl_->crumb, {pane, 0.0f, std::max(0.0f, absolute.w - pane), crumb_h});
            ArrangeChildAt(static_cast<size_t>(i));
        }
    }
    const int content_i = impl_->IndexOf(impl_->content);
    if (content_i >= 0) {
        SetChildBounds(*impl_->content, {pane, crumb_h, std::max(0.0f, absolute.w - pane),
                                         std::max(0.0f, absolute.h - crumb_h)});
        ArrangeChildAt(static_cast<size_t>(content_i));
    }
    impl_->BuildRows(absolute.h, pane);
}

Rect NavigationView::ChildrenClipBounds() const noexcept { return AbsoluteBounds(); }

void NavigationView::Draw(Painter& painter, const Theme& theme) {
    const float pane = impl_->Width(absolute_.w);
    const bool compact = impl_->Compact(absolute_.w);
    const Rect pane_rect{absolute_.x, absolute_.y, pane, absolute_.h};
    painter.FillRect(pane_rect, theme.fill_input);
    painter.FillRect({pane_rect.Right(), pane_rect.y, 1.0f, pane_rect.h}, theme.stroke_divider);
    const Rect toggle{absolute_.x + kPanePad, absolute_.y + kPanePad, pane - kPanePad * 2.0f, kRowHeight};
    if (impl_->hover == -2) painter.FillRoundedRect(toggle, 8.0f, theme.fill_hover);
    painter.DrawIcon(compact ? icon::kChevronRight : icon::kChevronLeft,
                     {toggle.x, toggle.y, impl_->compact_length - kPanePad * 2.0f, toggle.h}, 11.0f,
                     theme.text_secondary);
    if (!compact) painter.DrawText(L"Navigation", {toggle.x + 40.0f, toggle.y, toggle.w - 44.0f, toggle.h}, TextRole::BodyStrong, theme.text);
    if (impl_->search_enabled && compact) {
        const Rect search_slot{absolute_.x + kPanePad, absolute_.y + kPanePad + kRowHeight + kSearchGap,
                               pane - kPanePad * 2.0f, kRowHeight};
        if (impl_->hover == -3) painter.FillRoundedRect(search_slot, 8.0f, theme.fill_hover);
        painter.DrawIcon(icon::kSearch, {search_slot.x, search_slot.y, impl_->compact_length - kPanePad * 2.0f,
                                         search_slot.h},
                         16.0f, theme.text_secondary);
    }

    const auto draw_rows = [&](bool footer) {
        for (size_t row_index = 0; row_index < impl_->rows.size(); ++row_index) {
            const auto& row = impl_->rows[row_index];
            if (row.footer != footer) continue;
            const NavigationItem& item = impl_->ItemAt(row);
            const Rect r{absolute_.x + row.rect.x, absolute_.y + row.rect.y, row.rect.w, row.rect.h};
            if (item.type == NavigationItemType::Separator) { painter.FillRect({r.x + 6.0f, r.y + 5.0f, r.w - 12.0f, 1.0f}, theme.stroke_divider); continue; }
            if (item.type == NavigationItemType::Header) { if (!compact) painter.DrawText(item.text, {r.x + 10.0f, r.y, r.w - 20.0f, r.h}, TextRole::Overline, theme.text_secondary); continue; }
            const bool selected = item.id == impl_->selected;
            if (selected) painter.FillRoundedRect(r, 8.0f, theme.fill_selected);
            else if (static_cast<int>(row_index) == impl_->hover) painter.FillRoundedRect(r, 8.0f, theme.fill_hover);
            if (selected) painter.FillRoundedRect({r.x, r.y + 9.0f, 3.0f, r.h - 18.0f}, 1.5f, theme.accent);
            const float icon_width = impl_->compact_length - kPanePad * 2.0f;
            const float indent = compact ? 0.0f : row.depth * kIndent;
            const bool has_chevron = !row.footer && impl_->HasChildren(row.index);
            painter.DrawIcon(item.glyph.empty() ? icon::kLayers : item.glyph,
                             {r.x + indent, r.y, icon_width, r.h}, 16.0f, item.enabled ? theme.text : theme.text_disabled);
            if (!compact) {
                float trail = 28.0f;
                if (!item.badge.Empty()) trail += MeasureInfoBadge(item.badge).w + 6.0f;
                painter.DrawText(item.text, {r.x + indent + icon_width, r.y,
                                             r.w - indent - icon_width - trail, r.h}, TextRole::Body,
                                 item.enabled ? theme.text : theme.text_disabled);
                if (!item.badge.Empty()) {
                    const Size sz = MeasureInfoBadge(item.badge);
                    const float chevron = has_chevron ? 18.0f : 0.0f;
                    PaintInfoBadge(painter, theme,
                                   {r.Right() - 8.0f - chevron - sz.w * 0.5f, r.y + r.h * 0.5f},
                                   item.badge);
                }
                if (has_chevron) {
                    painter.DrawChevron({r.Right() - 15.0f, r.y + r.h * 0.5f}, 9.0f,
                                        impl_->expanded.count(item.id) ? 0.0f : -90.0f,
                                        theme.text_secondary, 1.4f);
                }
            } else if (!item.badge.Empty()) {
                PaintInfoBadge(painter, theme, {r.x + indent + icon_width - 2.0f, r.y + 8.0f},
                               item.badge);
            }
            if (focused_ && static_cast<int>(row_index) == impl_->focus)
                PaintFocusRing(painter, theme, r, 8.0f);
        }
    };
    const Rect main_clip{pane_rect.x, pane_rect.y + impl_->main_top, pane_rect.w,
                         std::max(0.0f, impl_->footer_top - impl_->main_top)};
    painter.PushClip(main_clip);
    draw_rows(false);
    painter.PopClip();
    const Rect footer_clip{pane_rect.x, pane_rect.y + impl_->footer_top, pane_rect.w,
                           std::max(0.0f, pane_rect.h - impl_->footer_top)};
    painter.PushClip(footer_clip);
    draw_rows(true);
    painter.PopClip();
    painter.DrawScrollThumb(MakeScrollThumb(main_clip,
                                             impl_->main_content_height, impl_->scroll_y, 1.0f, true),
                            theme.scrollbar_thumb);
    if (impl_->crumb && impl_->crumb_visible) {
        const float crumb_h = impl_->crumb->DesiredSize().h;
        painter.FillRect({absolute_.x + pane, absolute_.y + std::max(crumb_h - 1.0f, 0.0f),
                          std::max(0.0f, absolute_.w - pane), 1.0f},
                         theme.stroke_divider);
    }
}

bool NavigationView::OnKey(uint32_t vk) {
    if (impl_->search_enabled && (vk == 'F' || vk == 'f') &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        if (impl_->Compact(absolute_.w)) {
            impl_->ExpandPane();
            Relayout();
        }
        impl_->EnsureSearch();
        if (impl_->search) impl_->search->FocusInput();
        return true;
    }
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_HOME || vk == VK_END) {
        const int direction = vk == VK_UP ? -1 : 1;
        const int start = vk == VK_HOME ? 0 : vk == VK_END ? static_cast<int>(impl_->rows.size()) - 1 : impl_->focus + direction;
        const int next = impl_->FirstSelectable(start, direction);
        if (next >= 0) impl_->focus = next;
        Invalidate(); return true;
    }
    if (vk == VK_RETURN || vk == VK_SPACE) { impl_->Select(impl_->focus, true); return true; }
    if (impl_->focus >= 0 && impl_->focus < static_cast<int>(impl_->rows.size())) {
        const auto& row = impl_->rows[static_cast<size_t>(impl_->focus)];
        if (!row.footer && impl_->HasChildren(row.index)) {
            const NavigationItem& item = impl_->items[row.index];
            if (vk == VK_LEFT) { ItemExpanded(item.id, false); return true; }
            if (vk == VK_RIGHT) { ItemExpanded(item.id, true); return true; }
        }
    }
    return false;
}

void NavigationView::OnMouseMove(Point local, uint32_t) {
    const int hit = impl_->HitPane(local, absolute_.w);
    if (hit != impl_->hover) {
        impl_->hover = hit;
        if (hit >= 0 && impl_->Compact(absolute_.w)) {
            const auto& item = impl_->ItemAt(impl_->rows[static_cast<size_t>(hit)]);
            tooltip_ = item.type == NavigationItemType::Item ? item.text : std::wstring{};
        } else {
            tooltip_.clear();
        }
        Invalidate();
    }
}

void NavigationView::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & MK_LBUTTON)) return;
    Focus();
    const int chrome = impl_->HitPane(local, absolute_.w);
    if (chrome == -2) {
        impl_->TogglePane(absolute_.w);
        Relayout();
        return;
    }
    if (chrome == -3) {
        impl_->ExpandPane();
        Relayout();
        impl_->EnsureSearch();
        if (impl_->search) impl_->search->FocusInput();
        return;
    }
    const int hit = chrome;
    if (hit < 0) return;
    const auto& row = impl_->rows[static_cast<size_t>(hit)];
    const NavigationItem& item = impl_->ItemAt(row);
    if (row.footer || item.type != NavigationItemType::Item || !item.enabled) { impl_->Select(hit, true); return; }
    if (impl_->Compact(absolute_.w) && impl_->HasChildren(row.index)) {
        impl_->popup.Rebuild(item.id);
        WindowImpl::ShowTransient(window_, &impl_->popup, this, 260.0f, false);
        return;
    }
    if (!impl_->Compact(absolute_.w) && impl_->HasChildren(row.index) &&
        local.x >= impl_->Width(absolute_.w) - 36.0f) {
        ItemExpanded(item.id, !ItemExpanded(item.id));
        return;
    }
    impl_->Select(hit, true);
}

bool NavigationView::OnWheel(float delta) {
    if (impl_->max_scroll <= 0.0f) return false;
    impl_->scroll_y = Clamp(impl_->scroll_y - delta * kRowHeight * 2.5f, 0.0f, impl_->max_scroll);
    Relayout();
    return true;
}

void NavigationView::OnMouseLeave() { Control::OnMouseLeave(); impl_->hover = -1; tooltip_.clear(); Invalidate(); }
void NavigationView::OnFocusChanged(bool focused) { focused_ = focused; if (focused && impl_->focus < 0) impl_->focus = impl_->FirstSelectable(0, 1); Invalidate(); }
CursorShape NavigationView::CursorAt(Point local) const {
    return impl_->PaneTargetClickable(impl_->HitPane(local, absolute_.w)) ? CursorShape::Hand
                                                                         : CursorShape::Arrow;
}

} // namespace lumen

// lumen/ItemsModel.h — 扁平项数据源：控件订阅变更信号后自动刷新；绘制经 Get 写入复用 Row。
// Events: OnInserted / OnRemoved / OnChanged / OnReset / OnDetached
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Signal.h"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lumen {

struct ItemRow {
    std::wstring text;
    std::wstring glyph;
    std::vector<std::wstring> cells;
};

struct ItemData {
    std::wstring text;
    std::wstring glyph;
};

class ItemsModel {
public:
    virtual ~ItemsModel() { detached_.Emit(); }
    virtual size_t Count() const noexcept = 0;
    // 写入 out（调用方保留容量）。越界则清空。
    virtual void Get(size_t index, ItemRow& out) const = 0;
    // 视图行 → 本模型底层源下标。VectorModel 恒等；过滤/排序装饰器重写。
    virtual size_t SourceIndex(size_t view) const noexcept { return view; }
    // 0 表示未提供稳定标识；非零 key 在插入、排序、过滤及 Reset 后保持业务身份。
    virtual uint64_t RowKey(size_t) const noexcept { return 0; }
    ptrdiff_t FindKey(uint64_t key) const noexcept {
        if (!key) return -1;
        for (size_t i = 0; i < Count(); ++i)
            if (RowKey(i) == key) return static_cast<ptrdiff_t>(i);
        return -1;
    }

    Connection OnInserted(std::function<void(size_t, size_t)> fn) {
        return inserted_.Connect(std::move(fn));
    }
    Connection OnRemoved(std::function<void(size_t, size_t)> fn) {
        return removed_.Connect(std::move(fn));
    }
    Connection OnChanged(std::function<void(size_t, size_t)> fn) {
        return changed_.Connect(std::move(fn));
    }
    Connection OnReset(std::function<void()> fn) { return reset_.Connect(std::move(fn)); }
    Connection OnDetached(std::function<void()> fn) { return detached_.Connect(std::move(fn)); }

protected:
    void NotifyInserted(size_t index, size_t count = 1) {
        if (!DeferMutation(true, index, count)) inserted_.Emit(index, count);
    }
    void NotifyRemoved(size_t index, size_t count = 1) {
        if (!DeferMutation(true, index, count)) removed_.Emit(index, count);
    }
    void NotifyChanged(size_t index, size_t count = 1) {
        if (!DeferMutation(false, index, count)) changed_.Emit(index, count);
    }
    void NotifyReset() { if (!DeferMutation(true, 0, 0)) reset_.Emit(); }

private:
    bool DeferMutation(bool reset, size_t index, size_t count) {
        if (!UpdateScope::Active()) return false;
        pending_reset_ |= reset;
        pending_first_ = std::min(pending_first_, index);
        pending_end_ = std::max(pending_end_, index + count);
        UpdateScope::Defer(this, [this, weak = std::weak_ptr<int>(life_)] {
            if (weak.expired()) return;
            const bool reset = pending_reset_;
            const size_t first = pending_first_, end = pending_end_;
            pending_reset_ = false; pending_first_ = static_cast<size_t>(-1); pending_end_ = 0;
            if (reset) reset_.Emit();
            else if (end > first) changed_.Emit(first, end - first);
        });
        return true;
    }
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    bool pending_reset_ = false;
    size_t pending_first_ = static_cast<size_t>(-1), pending_end_ = 0;
    Signal<size_t, size_t> inserted_;
    Signal<size_t, size_t> removed_;
    Signal<size_t, size_t> changed_;
    Signal<> reset_;
    Signal<> detached_;
};

class TreeModel {
public:
    static constexpr size_t kRoot = static_cast<size_t>(-1);
    virtual ~TreeModel() { detached_.Emit(); }
    virtual size_t ChildCount(size_t parent) const = 0;
    virtual size_t Child(size_t parent, size_t index) const = 0;
    virtual void Get(size_t id, ItemRow& out) const = 0;
    Connection OnDetached(std::function<void()> fn) { return detached_.Connect(std::move(fn)); }
    Connection OnReset(std::function<void()> fn) { return reset_.Connect(std::move(fn)); }

protected:
    void NotifyReset() { reset_.Emit(); }

private:
    Signal<> detached_;
    Signal<> reset_;
};

template <class T>
class VectorModel : public ItemsModel {
public:
    VectorModel() = default;
    explicit VectorModel(std::vector<T> items) : items_(std::move(items)) {}

    VectorModel& Map(std::function<void(const T&, ItemRow&)> fn) {
        map_ = std::move(fn);
        NotifyReset();
        return *this;
    }

    size_t Count() const noexcept override { return items_.size(); }
    VectorModel& Key(std::function<uint64_t(const T&)> key) {
        key_ = std::move(key);
        NotifyReset();
        return *this;
    }
    uint64_t RowKey(size_t index) const noexcept override {
        return key_ && index < items_.size() ? key_(items_[index]) : 0;
    }
    void Get(size_t index, ItemRow& out) const override {
        out.text.clear();
        out.glyph.clear();
        out.cells.clear();
        if (index >= items_.size()) return;
        if (map_) {
            map_(items_[index], out);
            return;
        }
        FillDefault(items_[index], out);
    }

    const T& At(size_t index) const { return items_[index]; }
    T& At(size_t index) { return items_[index]; }
    const T* TryAt(ptrdiff_t index) const {
        if (index < 0 || static_cast<size_t>(index) >= items_.size()) return nullptr;
        return &items_[static_cast<size_t>(index)];
    }
    const std::vector<T>& Items() const noexcept { return items_; }

    void Push(T value) { Insert(items_.size(), std::move(value)); }
    void Insert(size_t index, T value) {
        if (index > items_.size()) index = items_.size();
        items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), std::move(value));
        NotifyInserted(index, 1);
    }
    void RemoveAt(size_t index) {
        if (index >= items_.size()) return;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        NotifyRemoved(index, 1);
    }
    void At(size_t index, T value) {
        if (index >= items_.size()) return;
        items_[index] = std::move(value);
        NotifyChanged(index, 1);
    }
    void Reset(std::vector<T> items) {
        items_ = std::move(items);
        NotifyReset();
    }

    template <class View>
    const T* Selected(const View& view) const {
        return TryAt(view.SelectedDataIndex());
    }

private:
    static void FillDefault(const T& value, ItemRow& out) {
        if constexpr (std::is_same_v<T, std::wstring>) {
            out.text = value;
        } else if constexpr (std::is_same_v<T, ItemData>) {
            out.text = value.text;
            out.glyph = value.glyph;
        } else {
            out.text = L"<VectorModel: call Map()>";
            (void)value;
        }
    }

    std::vector<T> items_;
    std::function<void(const T&, ItemRow&)> map_;
    std::function<uint64_t(const T&)> key_;
};

class FilteredModel : public ItemsModel {
public:
    explicit FilteredModel(std::shared_ptr<ItemsModel> source,
                           std::function<bool(size_t, const ItemRow&)> pred = {})
        : FilteredModel(RequireSource(source), std::move(pred)) { owned_source_ = std::move(source); }
    explicit FilteredModel(ItemsModel& source,
                           std::function<bool(size_t, const ItemRow&)> pred = {})
        : source_(&source), pred_(std::move(pred)) {
        BindSource();
        Rebuild();
    }

    FilteredModel& Where(std::function<bool(size_t, const ItemRow&)> pred) {
        pred_ = std::move(pred);
        Rebuild();
        NotifyReset();
        return *this;
    }

    size_t Count() const noexcept override { return index_.size(); }
    void Get(size_t index, ItemRow& out) const override {
        if (index >= index_.size() || !source_) {
            out.text.clear();
            out.glyph.clear();
            out.cells.clear();
            return;
        }
        source_->Get(index_[index], out);
    }
    size_t SourceIndex(size_t view) const noexcept override {
        if (view >= index_.size() || !source_) return view;
        return source_->SourceIndex(index_[view]);
    }
    uint64_t RowKey(size_t view) const noexcept override {
        return source_ && view < index_.size() ? source_->RowKey(index_[view]) : 0;
    }

private:
    static ItemsModel& RequireSource(const std::shared_ptr<ItemsModel>& source) {
        if (!source) throw std::invalid_argument("FilteredModel requires a source");
        return *source;
    }
    void BindSource() {
        if (!source_) return;
        inserted_ = ScopedConnection(source_->OnInserted([this](size_t, size_t) { OnSourceMut(); }));
        removed_ = ScopedConnection(source_->OnRemoved([this](size_t, size_t) { OnSourceMut(); }));
        changed_ = ScopedConnection(source_->OnChanged([this](size_t, size_t) { OnSourceMut(); }));
        reset_ = ScopedConnection(source_->OnReset([this] { OnSourceMut(); }));
        detached_ = ScopedConnection(source_->OnDetached([this] {
            source_ = nullptr;
            index_.clear();
            NotifyReset();
        }));
    }
    void OnSourceMut() {
        Rebuild();
        NotifyReset();
    }
    void Rebuild() {
        index_.clear();
        if (!source_) return;
        const size_t n = source_->Count();
        index_.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (!pred_) {
                index_.push_back(i);
                continue;
            }
            scratch_.text.clear();
            scratch_.glyph.clear();
            scratch_.cells.clear();
            source_->Get(i, scratch_);
            if (pred_(i, scratch_)) index_.push_back(i);
        }
    }

    ItemsModel* source_ = nullptr;
    std::shared_ptr<ItemsModel> owned_source_;
    std::function<bool(size_t, const ItemRow&)> pred_;
    std::vector<size_t> index_;
    mutable ItemRow scratch_;
    ScopedConnection detached_;
    ScopedConnection inserted_;
    ScopedConnection removed_;
    ScopedConnection changed_;
    ScopedConnection reset_;
};

class SortedModel : public ItemsModel {
public:
    explicit SortedModel(std::shared_ptr<ItemsModel> source, std::function<bool(size_t, size_t)> less = {})
        : SortedModel(RequireSource(source), std::move(less)) { owned_source_ = std::move(source); }
    explicit SortedModel(ItemsModel& source, std::function<bool(size_t, size_t)> less = {})
        : source_(&source), less_(std::move(less)) {
        BindSource();
        Rebuild();
    }

    SortedModel& OrderBy(std::function<bool(size_t, size_t)> less) {
        less_ = std::move(less);
        Rebuild();
        NotifyReset();
        return *this;
    }
    SortedModel& ClearOrder() { return OrderBy({}); }

    size_t Count() const noexcept override { return source_ ? source_->Count() : 0; }
    void Get(size_t index, ItemRow& out) const override {
        if (!source_ || index >= order_.size()) {
            out.text.clear();
            out.glyph.clear();
            out.cells.clear();
            return;
        }
        source_->Get(order_[index], out);
    }
    size_t SourceIndex(size_t view) const noexcept override {
        if (view >= order_.size() || !source_) return view;
        return source_->SourceIndex(order_[view]);
    }
    uint64_t RowKey(size_t view) const noexcept override {
        return source_ && view < order_.size() ? source_->RowKey(order_[view]) : 0;
    }

private:
    static ItemsModel& RequireSource(const std::shared_ptr<ItemsModel>& source) {
        if (!source) throw std::invalid_argument("SortedModel requires a source");
        return *source;
    }
    void BindSource() {
        if (!source_) return;
        inserted_ = ScopedConnection(source_->OnInserted([this](size_t, size_t) { OnSourceMut(); }));
        removed_ = ScopedConnection(source_->OnRemoved([this](size_t, size_t) { OnSourceMut(); }));
        changed_ = ScopedConnection(source_->OnChanged([this](size_t, size_t) { OnSourceMut(); }));
        reset_ = ScopedConnection(source_->OnReset([this] { OnSourceMut(); }));
        detached_ = ScopedConnection(source_->OnDetached([this] {
            source_ = nullptr;
            order_.clear();
            NotifyReset();
        }));
    }
    void OnSourceMut() {
        Rebuild();
        NotifyReset();
    }
    void Rebuild() {
        order_.clear();
        if (!source_) return;
        const size_t n = source_->Count();
        order_.resize(n);
        for (size_t i = 0; i < n; ++i) order_[i] = i;
        if (less_ && n > 1) {
            std::sort(order_.begin(), order_.end(), less_);
        }
    }

    ItemsModel* source_ = nullptr;
    std::shared_ptr<ItemsModel> owned_source_;
    std::function<bool(size_t, size_t)> less_;
    std::vector<size_t> order_;
    ScopedConnection detached_;
    ScopedConnection inserted_;
    ScopedConnection removed_;
    ScopedConnection changed_;
    ScopedConnection reset_;
};

} // namespace lumen

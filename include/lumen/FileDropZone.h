// lumen/FileDropZone.h — 从资源管理器拖入文件（OLE CF_HDROP）。
// Events: OnDrop / BindDrop
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class FileDropZone : public ControlOf<FileDropZone> {
public:
    FileDropZone();

    FileDropZone& Title(std::wstring_view value) {
        title_ = value;
        Invalidate();
        return *this;
    }
    const std::wstring& Title() const noexcept { return title_; }
    FileDropZone& Hint(std::wstring_view value) {
        hint_ = value;
        Invalidate();
        return *this;
    }
    const std::wstring& Hint() const noexcept { return hint_; }
    FileDropZone& Glyph(std::wstring_view value) {
        glyph_ = value;
        Invalidate();
        return *this;
    }
    // 分号 / 逗号分隔的扩展名，空串接受全部。例：".png;.jpg"
    FileDropZone& Accept(std::wstring_view extensions);
    FileDropZone& Multiple(bool value) {
        multiple_ = value;
        return *this;
    }
    bool Multiple() const noexcept { return multiple_; }
    FileDropZone& ZoneHeight(float value);
    float ZoneHeight() const noexcept { return height_; }

    std::vector<std::wstring> Filter(std::vector<std::wstring> paths) const;
    FileDropZone& OnDrop(std::function<void(const std::vector<std::wstring>&)> handler) {
        dropped_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindDrop(std::function<void(const std::vector<std::wstring>&)> handler) {
        return dropped_.Connect(std::move(handler));
    }
    const std::vector<std::wstring>& LastPaths() const noexcept { return last_; }
    bool Armed() const noexcept { return armed_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;
    bool AcceptsFileDrop() const noexcept override;
    std::vector<std::wstring> FilterFileDrop(std::vector<std::wstring> paths) const override;
    void OnFileDrag(bool over) override;
    void OnFileDrop(std::vector<std::wstring> paths) override;
    CursorShape CursorAt(Point local) const override;
    float ChromeRadius(const Theme& theme) const noexcept override;

    void RelayoutParent();

    std::wstring title_;
    std::wstring hint_;
    std::wstring glyph_;
    std::vector<std::wstring> accept_;
    std::vector<std::wstring> last_;
    Signal<const std::vector<std::wstring>&> dropped_;
    float height_ = 128.0f;
    bool multiple_ = true;
    bool armed_ = false;
    float armed_t_ = 0.0f;
};

} // namespace lumen

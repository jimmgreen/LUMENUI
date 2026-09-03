// lumen/ColorPicker.h — HSV 色板：饱和度/明度平面 + 色相条 + hex 输入/复制。铬层走 token。
// Events: OnColorChanged / BindColorChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <memory>
#include <string>

namespace lumen {

class TextBox;

class ColorPicker : public PanelOf<ColorPicker> {
public:
    ColorPicker();
    ~ColorPicker() override;

    lumen::Color Color() const noexcept;
    ColorPicker& Color(lumen::Color value);   // 不触发 OnColorChanged
    ColorPicker& OnColorChanged(std::function<void(lumen::Color)> handler);
    Connection BindColorChanged(std::function<void(lumen::Color)> handler);

    std::wstring Hex() const;
    bool Hex(std::wstring_view text);   // 解析成功则通知 OnColorChanged
    bool CopyHex() const;

    bool Enabled() const noexcept { return Control::Enabled(); }
    ColorPicker& Enabled(bool value);

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Prepare(Painter& painter) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool PrefersDragOverPan() const noexcept override { return true; }
    bool OnAnimate(float dt_seconds) override;
    CursorShape CursorAt(Point local) const override;

private:
    void ApplyHexFromField();
    void SyncHexField(bool force);
    void PlaceHexField();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TextBox* hex_ = nullptr;
};
} // namespace lumen

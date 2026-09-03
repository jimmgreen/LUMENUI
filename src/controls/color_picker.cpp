#include "lumen/ColorPicker.h"
#include "lumen/Clipboard.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "lumen/TextBox.h"
#include "../core/com_ptr.h"
#include <windows.h>
#include <d2d1_3.h>
#include "lumen/win_undef.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <vector>

namespace lumen {
namespace {

constexpr float kPad = 12.0f;
constexpr float kHueW = 14.0f;
constexpr float kGap = 10.0f;
constexpr float kPreview = 32.0f;
constexpr float kFooter = 40.0f;
constexpr float kSvMaxH = 180.0f;
constexpr float kCopyW = 28.0f;
constexpr uint32_t kMkLeft = 0x0001;

bool AxisFinite(float v) noexcept { return v >= 0.0f && v < 1.0e4f; }

void RgbToHsv(Color c, float& h, float& s, float& v) {
    const float mx = std::max(c.r, std::max(c.g, c.b));
    const float mn = std::min(c.r, std::min(c.g, c.b));
    v = mx;
    const float d = mx - mn;
    s = mx > 1.0e-6f ? d / mx : 0.0f;
    if (d < 1.0e-6f) return;
    if (mx == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0f : 0.0f);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
    else h = (c.r - c.g) / d + 4.0f;
    h /= 6.0f;
}

Color HsvToRgb(float h, float s, float v, float a) {
    h = h - std::floor(h);
    if (h < 0.0f) h += 1.0f;
    s = Clamp(s, 0.0f, 1.0f);
    v = Clamp(v, 0.0f, 1.0f);
    const float sector = h * 6.0f;
    const int i = static_cast<int>(std::floor(sector)) % 6;
    const float f = sector - std::floor(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return {r, g, b, a};
}

uint8_t ToByte(float c) {
    return static_cast<uint8_t>(Clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
}

std::wstring FormatHex(lumen::Color c) {
    wchar_t buf[12]{};
    swprintf_s(buf, L"#%02X%02X%02X", ToByte(c.r), ToByte(c.g), ToByte(c.b));
    return buf;
}

bool ParseHex(std::wstring_view text, float alpha, lumen::Color& out) {
    size_t begin = 0;
    while (begin < text.size() && iswspace(text[begin])) ++begin;
    size_t end = text.size();
    while (end > begin && iswspace(text[end - 1])) --end;
    if (begin < end && text[begin] == L'#') ++begin;
    const size_t n = end - begin;
    if (n != 3 && n != 6) return false;
    unsigned value = 0;
    for (size_t i = 0; i < n; ++i) {
        const wchar_t ch = text[begin + i];
        unsigned digit = 0;
        if (ch >= L'0' && ch <= L'9') digit = static_cast<unsigned>(ch - L'0');
        else if (ch >= L'A' && ch <= L'F') digit = static_cast<unsigned>(ch - L'A' + 10);
        else if (ch >= L'a' && ch <= L'f') digit = static_cast<unsigned>(ch - L'a' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    if (n == 3) {
        const unsigned r = (value >> 8) & 0xF;
        const unsigned g = (value >> 4) & 0xF;
        const unsigned b = value & 0xF;
        value = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
    }
    out = Color::Hex(value, alpha);
    return true;
}

bool CopyUnicode(std::wstring_view text) {
    return clipboard::Text(text);
}

Rect SvRect(const Rect& box) {
    const float sv_w = std::max(0.0f, box.w - kPad * 2.0f - kHueW - kGap);
    const float sv_h = std::max(0.0f, box.h - kPad * 2.0f - kGap - kFooter);
    return {box.x + kPad, box.y + kPad, sv_w, sv_h};
}
Rect HueRect(const Rect& box) {
    const Rect sv = SvRect(box);
    return {sv.Right() + kGap, sv.y, kHueW, sv.h};
}
Rect PreviewRect(const Rect& box) {
    const Rect sv = SvRect(box);
    const float y = sv.Bottom() + kGap + (kFooter - kPreview) * 0.5f;
    return {box.x + kPad, y, kPreview, kPreview};
}
Rect CopyRect(const Rect& box) {
    const Rect preview = PreviewRect(box);
    return {box.Right() - kPad - kCopyW, preview.y, kCopyW, preview.h};
}
Rect HexRect(const Rect& box) {
    const Rect preview = PreviewRect(box);
    const Rect copy = CopyRect(box);
    const float x = preview.Right() + 8.0f;
    const float y = SvRect(box).Bottom() + kGap;
    return {x, y, std::max(0.0f, copy.x - 6.0f - x), kFooter};
}

class HexField : public TextBox {
public:
    std::function<void(bool)> on_focus;

protected:
    bool ImeInline() const noexcept override { return false; }
    void OnFocusChanged(bool focused) override {
        TextBox::OnFocusChanged(focused);
        if (focused && !text_.empty()) {
            SetCaret(0, false, false);
            SetCaret(text_.size(), true, false);
        }
        if (on_focus) on_focus(focused);
    }
    bool OnChar(wchar_t ch) override {
        if (ch < 32) return TextBox::OnChar(ch);
        if (ch != L'#' && !iswxdigit(ch)) return true;
        const wchar_t up = ch == L'#' ? ch : static_cast<wchar_t>(towupper(ch));
        if (!HasSelection()) {
            if (up == L'#') {
                if (text_.find(L'#') != std::wstring::npos) return true;
            } else {
                size_t digits = 0;
                for (wchar_t c : text_) {
                    if (iswxdigit(c)) ++digits;
                }
                if (digits >= 6) return true;
            }
            if (text_.size() >= 7) return true;
        }
        return TextBox::OnChar(up);
    }
};

} // namespace

struct ColorPicker::Impl {
    float h = 0.0f;
    float s = 1.0f;
    float v = 1.0f;
    float alpha = 1.0f;
    float glow_t = 0.0f;
    float copy_t = 0.0f;
    int drag = 0;   // 0 无 / 1 SV / 2 色相
    Signal<lumen::Color> changed;
    std::vector<uint8_t> pixels;
    ComPtr<ID2D1Bitmap1> bitmap;
    void* device = nullptr;
    float baked_h = -1.0f;
    int baked_w = 0;
    int baked_h_px = 0;

    lumen::Color Rgb() const { return HsvToRgb(h, s, v, alpha); }

    void FromRgb(lumen::Color color) {
        alpha = color.a;
        RgbToHsv(color, h, s, v);
    }

    void Notify() {
        changed.Emit(Rgb());
    }
};

ColorPicker::ColorPicker() : impl_(std::make_unique<Impl>()) {
    auto& field = Add<HexField>();
    hex_ = &field;
    field.Text(FormatHex(impl_->Rgb()));
    field.Placeholder(L"#RRGGBB");
    field.OnTextChanged([this](std::wstring_view) { ApplyHexFromField(); });
    field.OnSubmit([this] { ApplyHexFromField(); });
    field.on_focus = [this](bool focused) {
        if (focused) return;
        lumen::Color parsed{};
        if (!ParseHex(hex_->Text(), impl_->alpha, parsed)) SyncHexField(true);
    };
}

ColorPicker::~ColorPicker() = default;

ColorPicker& ColorPicker::Enabled(bool value) {
    Control::Enabled(value);
    if (hex_) hex_->Enabled(value);
    return *this;
}

lumen::Color ColorPicker::Color() const noexcept { return impl_->Rgb(); }

ColorPicker& ColorPicker::Color(lumen::Color value) {
    impl_->FromRgb(value);
    impl_->baked_h = -1.0f;
    SyncHexField(true);
    Invalidate();
    return *this;
}

ColorPicker& ColorPicker::OnColorChanged(std::function<void(lumen::Color)> handler) {
    impl_->changed.Subscribe(std::move(handler));
    return *this;
}
Connection ColorPicker::BindColorChanged(std::function<void(lumen::Color)> handler) {
    return impl_->changed.Connect(std::move(handler));
}

std::wstring ColorPicker::Hex() const { return FormatHex(impl_->Rgb()); }

bool ColorPicker::Hex(std::wstring_view text) {
    lumen::Color parsed{};
    if (!ParseHex(text, impl_->alpha, parsed)) return false;
    impl_->FromRgb(parsed);
    impl_->baked_h = -1.0f;
    SyncHexField(true);
    Invalidate();
    impl_->Notify();
    return true;
}

bool ColorPicker::CopyHex() const {
    return CopyUnicode(Hex());
}

void ColorPicker::ApplyHexFromField() {
    if (!hex_) return;
    lumen::Color parsed{};
    if (!ParseHex(hex_->Text(), impl_->alpha, parsed)) return;
    impl_->FromRgb(parsed);
    impl_->baked_h = -1.0f;
    impl_->Notify();
    Invalidate();
}

void ColorPicker::SyncHexField(bool force) {
    if (!hex_) return;
    if (!force && hex_->HasFocus()) return;
    hex_->Text(FormatHex(impl_->Rgb()));
}

void ColorPicker::PlaceHexField() {
    if (!hex_) return;
    const Rect hex = HexRect(absolute_);
    SetChildBounds(*hex_, {hex.x - absolute_.x, hex.y - absolute_.y, hex.w, hex.h});
    ArrangeChildAt(0);
}

Size ColorPicker::Measure(Size available, const Theme& theme) {
    const float width = AxisFinite(available.w) ? available.w : 280.0f;
    const float sv_w = std::max(0.0f, width - kPad * 2.0f - kHueW - kGap);
    const float sv_h = std::min(sv_w, kSvMaxH);
    if (hex_) {
        const float hex_w = std::max(0.0f, sv_w - kPreview - kCopyW - 14.0f);
        MeasureChildAt(0, {hex_w, theme.input_height}, theme);
    }
    return {width, kPad + sv_h + kGap + kFooter + kPad};
}

void ColorPicker::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    PlaceHexField();
}

void ColorPicker::Prepare(Painter& painter) {
    const Rect sv = SvRect(absolute_);
    if (sv.IsEmpty()) return;
    const int w = std::max(8, static_cast<int>(sv.w + 0.5f));
    const int h = std::max(8, static_cast<int>(sv.h + 0.5f));
    const void* identity = painter.DeviceIdentity();
    if (impl_->bitmap && impl_->device == identity && impl_->baked_w == w &&
        impl_->baked_h_px == h && std::abs(impl_->baked_h - impl_->h) < 0.0005f) {
        painter.PrepareRoundedClip(sv, 8.0f);
        return;
    }
    impl_->pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    uint8_t* px = impl_->pixels.data();
    for (int y = 0; y < h; ++y) {
        const float vv = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            const float ss = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            const lumen::Color c = HsvToRgb(impl_->h, ss, vv, 1.0f);
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            px[i + 0] = ToByte(c.b);
            px[i + 1] = ToByte(c.g);
            px[i + 2] = ToByte(c.r);
            px[i + 3] = 255;
        }
    }
    impl_->bitmap.reset();
    impl_->bitmap.p = painter.CreateBitmapBgra(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                               px, static_cast<uint32_t>(w) * 4u);
    impl_->device = impl_->bitmap ? const_cast<void*>(identity) : nullptr;
    impl_->baked_h = impl_->h;
    impl_->baked_w = w;
    impl_->baked_h_px = h;
    painter.PrepareRoundedClip(sv, 8.0f);
}

void ColorPicker::Draw(Painter& painter, const Theme& theme) {
    const Rect box = absolute_;
    const float radius = theme.radius_control;
    painter.FillRoundedRect(box, radius, theme.fill_input);

    const Rect sv = SvRect(box);
    const Rect hue = HueRect(box);
    const Rect preview = PreviewRect(box);
    const Rect copy = CopyRect(box);
    painter.PushRoundedClip(sv, 8.0f);
    if (impl_->bitmap) painter.DrawBitmap(impl_->bitmap.get(), sv, true);
    painter.PopRoundedClip();
    painter.StrokeRoundedRect(sv, 8.0f, theme.stroke_card);

    constexpr int kHueSteps = 36;
    const float slice = hue.h / static_cast<float>(kHueSteps);
    for (int i = 0; i < kHueSteps; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(kHueSteps);
        const lumen::Color c = HsvToRgb(t0, 1.0f, 1.0f, 1.0f);
        painter.FillRect({hue.x, hue.y + slice * static_cast<float>(i), hue.w, slice + 0.5f}, c);
    }
    painter.StrokeRoundedRect(hue, 4.0f, theme.stroke_card);

    const float thumb_r = 6.0f;
    const Point sv_pos{sv.x + impl_->s * sv.w, sv.y + (1.0f - impl_->v) * sv.h};
    const Rect sv_thumb{sv_pos.x - thumb_r, sv_pos.y - thumb_r, thumb_r * 2.0f, thumb_r * 2.0f};
    lumen::Color glow = theme.glow_sm;
    glow.a *= Lerp(0.35f, 1.0f, impl_->glow_t) * theme.glow_intensity;
    if (glow.a > 0.004f) painter.DrawGlow(sv_thumb, thumb_r, glow);
    painter.FillRoundedRect(sv_thumb, thumb_r, theme.accent);
    painter.StrokeRoundedRect(sv_thumb, thumb_r, theme.bg, 1.5f);

    const float hy = hue.y + impl_->h * hue.h;
    const Rect hue_thumb{hue.x - 2.0f, hy - 4.0f, hue.w + 4.0f, 8.0f};
    if (glow.a > 0.004f) painter.DrawGlow(hue_thumb, 4.0f, glow);
    painter.FillRoundedRect(hue_thumb, 4.0f, theme.accent);
    painter.StrokeRoundedRect(hue_thumb, 4.0f, theme.bg, 1.0f);

    painter.FillRoundedRect(preview, 8.0f, impl_->Rgb());
    painter.StrokeRoundedRect(preview, 8.0f, theme.stroke_card);

    const bool copied = impl_->copy_t > 0.04f;
    if (copied) {
        lumen::Color wash = theme.fill_selected;
        wash.a *= impl_->copy_t;
        painter.FillRoundedRect(copy, 8.0f, wash);
    }
    painter.DrawIcon(copied ? icon::kCheckMark : icon::kCopy, copy, 14.0f, theme.text_secondary);
}

bool ColorPicker::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && (vk == 'C' || vk == 'c')) {
        if (CopyHex()) {
            impl_->copy_t = 1.0f;
            Animate();
        }
        return true;
    }
    if (ctrl && (vk == 'V' || vk == 'v')) {
        const std::wstring pasted = clipboard::Text();
        if (!pasted.empty()) Hex(pasted);
        return true;
    }
    float dh = 0.0f, ds = 0.0f, dv = 0.0f;
    switch (vk) {
    case VK_LEFT: ds = -0.02f; break;
    case VK_RIGHT: ds = 0.02f; break;
    case VK_UP: dv = 0.02f; break;
    case VK_DOWN: dv = -0.02f; break;
    case VK_PRIOR: dh = -0.04f; break;
    case VK_NEXT: dh = 0.04f; break;
    case VK_HOME: impl_->s = 0.0f; impl_->v = 1.0f; break;
    case VK_END: impl_->s = 1.0f; impl_->v = 1.0f; break;
    default: return false;
    }
    impl_->h = impl_->h + dh;
    impl_->h = impl_->h - std::floor(impl_->h);
    impl_->s = Clamp(impl_->s + ds, 0.0f, 1.0f);
    impl_->v = Clamp(impl_->v + dv, 0.0f, 1.0f);
    if (dh != 0.0f) impl_->baked_h = -1.0f;
    SyncHexField(false);
    Invalidate();
    impl_->Notify();
    return true;
}

void ColorPicker::OnFocusChanged(bool) {
    Invalidate();
}

void ColorPicker::OnMouseLeave() {
    Control::OnMouseLeave();
    impl_->drag = 0;
    Animate();
}

bool ColorPicker::OnAnimate(float dt_seconds) {
    const float target =
        (hovered_ || impl_->drag != 0 || (hex_ && hex_->HasFocus())) ? 1.0f : 0.0f;
    bool active = EaseTo(impl_->glow_t, target, dt_seconds);
    active |= EaseTo(impl_->copy_t, 0.0f, dt_seconds, 6.0f);
    return active;
}

CursorShape ColorPicker::CursorAt(Point) const {
    return CursorShape::Hand;
}

void ColorPicker::OnMouseDown(Point local, uint32_t buttons) {
    if (!enabled_ || !(buttons & kMkLeft)) return;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    const Rect box = absolute_;
    const Rect sv = SvRect(box);
    const Rect hue = HueRect(box);
    if (CopyRect(box).Contains(world)) {
        if (CopyHex()) {
            impl_->copy_t = 1.0f;
            Animate();
        }
        Focus();
        return;
    }
    if (sv.Contains(world)) impl_->drag = 1;
    else if (hue.Contains(world)) impl_->drag = 2;
    else return;
    pressed_ = true;
    Focus();
    Animate();
    OnMouseMove(local, buttons);
}

void ColorPicker::OnMouseMove(Point local, uint32_t buttons) {
    if (!enabled_ || impl_->drag == 0 || !(buttons & kMkLeft)) return;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    const Rect box = absolute_;
    if (impl_->drag == 1) {
        const Rect sv = SvRect(box);
        impl_->s = sv.w > 0.0f ? Clamp((world.x - sv.x) / sv.w, 0.0f, 1.0f) : 0.0f;
        impl_->v = sv.h > 0.0f ? Clamp(1.0f - (world.y - sv.y) / sv.h, 0.0f, 1.0f) : 1.0f;
    } else {
        const Rect hue = HueRect(box);
        impl_->h = hue.h > 0.0f ? Clamp((world.y - hue.y) / hue.h, 0.0f, 1.0f) : 0.0f;
        impl_->baked_h = -1.0f;
    }
    SyncHexField(false);
    Invalidate();
    impl_->Notify();
}

void ColorPicker::OnMouseUp(Point, uint32_t) {
    impl_->drag = 0;
    pressed_ = false;
    Animate();
}

} // namespace lumen

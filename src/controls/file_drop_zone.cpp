#include "lumen/FileDropZone.h"
#include "lumen/Icons.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace lumen {
namespace {

std::wstring Lower(std::wstring_view text) {
    std::wstring out(text);
    for (wchar_t& ch : out) ch = static_cast<wchar_t>(std::towlower(ch));
    return out;
}

std::wstring ExtensionOf(std::wstring_view path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos) return {};
    if (slash != std::wstring_view::npos && dot < slash) return {};
    return Lower(path.substr(dot));
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

FileDropZone::FileDropZone()
    : title_(L"Drop files here"), hint_(L"Release to add"), glyph_(icon::kOpenFile) {}

void FileDropZone::RelayoutParent() { Control::RelayoutParent(); }

FileDropZone& FileDropZone::Accept(std::wstring_view extensions) {
    accept_.clear();
    std::wstring token;
    auto flush = [&] {
        if (token.empty()) return;
        if (token[0] != L'.') token.insert(token.begin(), L'.');
        accept_.push_back(Lower(token));
        token.clear();
    };
    for (wchar_t ch : extensions) {
        if (ch == L';' || ch == L',' || std::iswspace(static_cast<wint_t>(ch))) flush();
        else token.push_back(ch);
    }
    flush();
    return *this;
}

FileDropZone& FileDropZone::ZoneHeight(float value) {
    height_ = std::max(72.0f, value);
    RelayoutParent();
    return *this;
}

std::vector<std::wstring> FileDropZone::Filter(std::vector<std::wstring> paths) const {
    std::vector<std::wstring> out;
    out.reserve(paths.size());
    for (std::wstring& path : paths) {
        if (path.empty()) continue;
        if (!accept_.empty()) {
            if (IsDirectory(path)) continue;
            const std::wstring ext = ExtensionOf(path);
            if (std::find(accept_.begin(), accept_.end(), ext) == accept_.end()) continue;
        }
        out.push_back(std::move(path));
        if (!multiple_) break;
    }
    return out;
}

bool FileDropZone::AcceptsFileDrop() const noexcept { return visible_ && enabled_; }

std::vector<std::wstring> FileDropZone::FilterFileDrop(std::vector<std::wstring> paths) const {
    return Filter(std::move(paths));
}

void FileDropZone::OnFileDrag(bool over) {
    if (armed_ == over) return;
    armed_ = over;
    Animate();
    Invalidate();
}

void FileDropZone::OnFileDrop(std::vector<std::wstring> paths) {
    last_ = Filter(std::move(paths));
    armed_ = false;
    Animate();
    Invalidate();
    if (!last_.empty()) dropped_.Emit(last_);
}

CursorShape FileDropZone::CursorAt(Point) const { return CursorShape::Hand; }

float FileDropZone::ChromeRadius(const Theme& theme) const noexcept { return theme.radius_control; }

Size FileDropZone::Measure(Size available, const Theme&) {
    const float width = (available.w >= 0.0f && available.w < 1.0e4f) ? available.w : 280.0f;
    return {width, height_};
}

bool FileDropZone::OnAnimate(float dt) {
    const bool more = EaseTo(armed_t_, armed_ ? 1.0f : 0.0f, dt);
    return more || Control::OnAnimate(dt);
}

void FileDropZone::Draw(Painter& painter, const Theme& theme) {
    const float radius = theme.radius_control;
    painter.FillRoundedRect(absolute_, radius, theme.fill_input);
    if (armed_t_ > 0.01f) {
        Color wash = theme.fill_hover;
        wash.a *= armed_t_;
        painter.FillRoundedRect(absolute_, radius, wash);
        painter.DrawGlow(absolute_, radius, theme.glow_sm);
    }
    const Color stroke = armed_t_ > 0.35f ? theme.accent : theme.stroke_card;
    painter.StrokeDashedRoundedRect(absolute_, radius, stroke);
    const float icon_size = 22.0f;
    const float text_h = 22.0f;
    const float hint_h = hint_.empty() ? 0.0f : 18.0f;
    const float block = icon_size + 10.0f + text_h + (hint_h > 0.0f ? 4.0f + hint_h : 0.0f);
    float y = absolute_.y + std::max(12.0f, (absolute_.h - block) * 0.5f);
    const Color fg = enabled_ ? theme.text : theme.text_disabled;
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, {absolute_.x, y, absolute_.w, icon_size}, icon_size, fg);
        y += icon_size + 10.0f;
    }
    painter.DrawText(title_, {absolute_.x + 16.0f, y, std::max(0.0f, absolute_.w - 32.0f), text_h},
                     TextRole::BodyStrong, fg, Align::Center);
    y += text_h;
    if (!hint_.empty()) {
        y += 4.0f;
        painter.DrawText(hint_, {absolute_.x + 16.0f, y, std::max(0.0f, absolute_.w - 32.0f), hint_h},
                         TextRole::Caption, theme.text_secondary, Align::Center);
    }
}

} // namespace lumen

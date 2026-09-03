#include "lumen/Flyout.h"
#include "lumen/Painter.h"
#include "lumen/Theme.h"
#include "../core/window_impl.h"

namespace lumen {

Flyout::Flyout() {
    Orientation(Orientation::Vertical);
    Spacing(8.0f);
    Padding(12.0f, 10.0f);
}

Flyout::~Flyout() {
    if (window_) WindowImpl::OverlayDestroyed(window_, this);
}

void Flyout::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    DrawElevated(painter, theme, absolute_, theme.radius_flyout, Elevation::Overlay, theme.bg);
}

} // namespace lumen

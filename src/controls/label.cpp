#include "fluentui/Label.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace fui {

Size Label::Measure(Size available, const Theme&) {
    if (text_.empty()) return {0.0f, role_ == TextRole::Title ? 28.0f : 20.0f};
    Size size = UiText().MeasureText(text_, role_,
                                     available.w > 0.0f ? available.w : 1.0e5f);
    // +2 DIP 余量：LumaText 与 DirectWrite 的排版宽度略有差异，避免末尾被
    // 当作溢出触发省略号截断。
    return {std::max(size.w + 2.0f, 1.0f), size.h};
}

void Label::Draw(Painter& painter, const Theme& theme) {
    if (text_.empty()) return;
    painter.DrawText(text_, absolute_, role_,
                     !enabled_ ? theme.text_disabled : (secondary_ ? theme.text_secondary : theme.text));
}

} // namespace fui

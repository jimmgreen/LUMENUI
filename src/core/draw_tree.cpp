// draw_tree.cpp — 控件子树绘制（离屏渲染与测试共用）。
#include "fluentui/Painter.h"
#include "fluentui/Panel.h"

namespace fui {

void DrawControlTree(Painter& painter, const Theme& theme, Control* root) {
    if (!root || !root->Visible()) return;
    root->Draw(painter, theme);
    if (auto* panel = dynamic_cast<Panel*>(root)) {
        for (size_t i = 0; i < panel->ChildCount(); ++i) {
            DrawControlTree(painter, theme, &panel->Child(i));
        }
    }
}

} // namespace fui

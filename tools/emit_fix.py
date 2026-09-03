# Mechanical Signal::Emit replacements in control .cpp files.
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

subs = {
    "src/controls/slider.cpp": [
        ("if (changed_) changed_(value_);", "changed_.Emit(value_);"),
    ],
    "src/controls/text_box.cpp": [
        ("if (text_changed_) text_changed_();", "text_changed_.Emit(text_);"),
        ("if (submit_) submit_();", "submit_.Emit();"),
    ],
    "src/controls/number_box.cpp": [
        ("if (notify && changed_) changed_(value);", "if (notify) changed_.Emit(value);"),
    ],
    "src/controls/checkbox.cpp": [
        ("if (toggled_) toggled_(Checked());", "toggled_.Emit(Checked());"),
    ],
    "src/controls/switch.cpp": [
        ("if (toggled_) toggled_(checked_);", "toggled_.Emit(checked_);"),
    ],
    "src/controls/radio_button.cpp": [
        ("if (toggled_) toggled_(true);", "toggled_.Emit(true);"),
    ],
    "src/controls/toggle_button.cpp": [
        ("if (toggled_) toggled_(checked_);", "toggled_.Emit(checked_);"),
    ],
    "src/controls/chip.cpp": [
        ("if (toggled_) toggled_(selected_);", "toggled_.Emit(selected_);"),
        ("if (closed_) closed_();", "closed_.Emit();"),
    ],
    "src/controls/combo_box.cpp": [
        ("if (changed && owner_->changed_) owner_->changed_();",
         "if (changed) owner_->changed_.Emit(owner_->selected_, owner_->selected_);"),
        ("if (changed_) changed_();", "changed_.Emit(selected_, selected_);"),
    ],
    "src/controls/list_view.cpp": [
        ("if (selection_changed_) selection_changed_();",
         "selection_changed_.Emit(selected_, SelectedDataIndex());"),
        ("activate_();", "activate_.Emit(selected_ >= 0 ? static_cast<size_t>(selected_) : 0);"),
    ],
    "src/controls/table.cpp": [
        ("if (selection_changed_) selection_changed_();",
         "selection_changed_.Emit(selected_, SelectedDataIndex());"),
    ],
    "src/controls/tab_control.cpp": [
        ("if (changed_) changed_();",
         "changed_.Emit(static_cast<ptrdiff_t>(selected_), static_cast<ptrdiff_t>(selected_));"),
        ("if (SelectedId() != previous_selected && changed_) changed_();",
         "if (SelectedId() != previous_selected) changed_.Emit(static_cast<ptrdiff_t>(selected_), static_cast<ptrdiff_t>(selected_));"),
    ],
    "src/controls/segmented.cpp": [
        ("if (changed_) changed_();", "changed_.Emit(selected_, selected_);"),
    ],
    "src/controls/grid_view.cpp": [
        ("if (selection_changed_) selection_changed_();",
         "selection_changed_.Emit(selected_, selected_);"),
        ("if (selected_ >= 0 && activate_) activate_();",
         "if (selected_ >= 0) activate_.Emit(static_cast<size_t>(selected_));"),
        ("if (activate_) activate_();",
         "activate_.Emit(selected_ >= 0 ? static_cast<size_t>(selected_) : 0);"),
    ],
    "src/controls/tree_view.cpp": [
        ("if (selection_changed_) selection_changed_();", "selection_changed_.Emit();"),
        ("if (selected_row_ >= 0 && activate_) {\n            activate_(visible_[static_cast<size_t>(selected_row_)]);",
         "if (selected_row_ >= 0) {\n            activate_.Emit(visible_[static_cast<size_t>(selected_row_)]);"),
        ("if (activate_) activate_(id);", "activate_.Emit(id);"),
    ],
    "src/controls/tree_table.cpp": [
        ("if (selection_changed_) selection_changed_();", "selection_changed_.Emit();"),
        ("if (selected_row_ >= 0 && activate_) {\n            activate_(visible_[static_cast<size_t>(selected_row_)]);",
         "if (selected_row_ >= 0) {\n            activate_.Emit(visible_[static_cast<size_t>(selected_row_)]);"),
        ("if (activate_) activate_(id);", "activate_.Emit(id);"),
    ],
    "src/controls/range_slider.cpp": [
        ("if (notify && changed_) changed_(lower_, upper_);", "if (notify) changed_.Emit(lower_, upper_);"),
        ("if (changed_) changed_(lower_, upper_);", "changed_.Emit(lower_, upper_);"),
    ],
}

for rel, pairs in subs.items():
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    orig = text
    for a, b in pairs:
        text = text.replace(a, b)
    if text != orig:
        path.write_text(text, encoding="utf-8", newline="\n")
        print("updated", rel)
    else:
        print("no change", rel)

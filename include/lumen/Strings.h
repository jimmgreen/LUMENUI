// lumen/Strings.h — 内置 UI 字串。默认按系统 UI 语言在中/英之间选择。
// Events: 无（本头无订阅事件）
// Keys: 无
// Layout: 非布局控件头，或见类声明
#pragma once
#include <string>

namespace lumen {

struct Strings {
    std::wstring ok;
    std::wstring cancel;
    std::wstring close;
    std::wstring yes;
    std::wstring no;
    std::wstring today;
    std::wstring clear;
    std::wstring search_placeholder;
    std::wstring busy;
    std::wstring busy_cancel;
    std::wstring empty_state_default;
    std::wstring required;
    std::wstring too_short;
    std::wstring invalid_format;
    std::wstring out_of_range;
    std::wstring cut;
    std::wstring copy;
    std::wstring paste;
    std::wstring select_all;

    static Strings English();
    static Strings Chinese();
    static Strings ForSystem();
};

inline Strings Strings::English() {
    Strings s;
    s.ok = L"OK";
    s.cancel = L"Cancel";
    s.close = L"Close";
    s.yes = L"Yes";
    s.no = L"No";
    s.today = L"Today";
    s.clear = L"Clear";
    s.search_placeholder = L"Search";
    s.busy = L"Please wait";
    s.busy_cancel = L"Cancel";
    s.empty_state_default = L"Nothing here";
    s.required = L"Required";
    s.too_short = L"Too short";
    s.invalid_format = L"Invalid format";
    s.out_of_range = L"Out of range";
    s.cut = L"Cut";
    s.copy = L"Copy";
    s.paste = L"Paste";
    s.select_all = L"Select all";
    return s;
}

inline Strings Strings::Chinese() {
    Strings s;
    s.ok = L"确定";
    s.cancel = L"取消";
    s.close = L"关闭";
    s.yes = L"是";
    s.no = L"否";
    s.today = L"今天";
    s.clear = L"清除";
    s.search_placeholder = L"搜索";
    s.busy = L"请稍候";
    s.busy_cancel = L"取消";
    s.empty_state_default = L"暂无内容";
    s.required = L"必填";
    s.too_short = L"太短";
    s.invalid_format = L"格式不正确";
    s.out_of_range = L"超出范围";
    s.cut = L"剪切";
    s.copy = L"复制";
    s.paste = L"粘贴";
    s.select_all = L"全选";
    return s;
}

} // namespace lumen

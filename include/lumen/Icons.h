// lumen/Icons.h -- Phosphor Regular glyphs + registration.
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
// DrawIcon paints these as filled 256-viewBox paths (weight 1.5 ≈ Bold). Unknown
// codepoints fall back to Segoe Fluent / MDL2. Phosphor Icons: MIT License.
#pragma once

namespace lumen::icon {

inline constexpr float kSize = 16.0f;    // control chrome
inline constexpr float kWeight = 1.5f;   // 1.0 Regular, 1.5 Bold-ish extra stroke

// Bind a Phosphor-style SVG `d` (viewBox 256) to a glyph so DrawIcon / IconView /
// Button::Glyph all paint it with the same fill + weight stroke as builtins.
// `svg_path` must outlive the process (string literal). Returns false if the
// glyph is already taken by a different path, or the extra table is full (64).
bool Register(wchar_t glyph, const char* svg_path, bool filled = true, bool fatten = true);

inline constexpr wchar_t kAdd[] = L"\uE710";
inline constexpr wchar_t kRemove[] = L"\uE738";
inline constexpr wchar_t kChevronDown[] = L"\uE70D";
inline constexpr wchar_t kChevronUp[] = L"\uE70E";
inline constexpr wchar_t kChevronRight[] = L"\uE76C";
inline constexpr wchar_t kChevronLeft[] = L"\uE76B";
inline constexpr wchar_t kClose[] = L"\uE8BB";
inline constexpr wchar_t kMinimize[] = L"\uE921";
inline constexpr wchar_t kMaximize[] = L"\uE922";
inline constexpr wchar_t kRestore[] = L"\uE923";
inline constexpr wchar_t kSearch[] = L"\uE721";
inline constexpr wchar_t kHome[] = L"\uE80F";
inline constexpr wchar_t kSettings[] = L"\uE713";
inline constexpr wchar_t kDelete[] = L"\uE74D";
inline constexpr wchar_t kCheckMark[] = L"\uE73E";
inline constexpr wchar_t kCheckSquare[] = L"\uE73A";
inline constexpr wchar_t kFolder[] = L"\uE8B7";
inline constexpr wchar_t kSave[] = L"\uE74E";
inline constexpr wchar_t kOpenFile[] = L"\uE8E5";
inline constexpr wchar_t kRefresh[] = L"\uE72C";
inline constexpr wchar_t kCopy[] = L"\uE8C8";
inline constexpr wchar_t kPaste[] = L"\uE77F";
inline constexpr wchar_t kCut[] = L"\uE8C6";
inline constexpr wchar_t kMail[] = L"\uE715";
inline constexpr wchar_t kFavorite[] = L"\uE734";
inline constexpr wchar_t kFavoriteFill[] = L"\uE735";
inline constexpr wchar_t kZap[] = L"\uE945";
inline constexpr wchar_t kWarning[] = L"\uE7BA";
inline constexpr wchar_t kInfo[] = L"\uE946";
inline constexpr wchar_t kShield[] = L"\uEA18";
inline constexpr wchar_t kBell[] = L"\uEA8F";
inline constexpr wchar_t kContact[] = L"\uE77B";
inline constexpr wchar_t kPackage[] = L"\uE7B8";
inline constexpr wchar_t kLayers[] = L"\uE81E";
inline constexpr wchar_t kCards[] = L"\uE8A9";
inline constexpr wchar_t kCode[] = L"\uE943";
// EA3B 为 Segoe Fluent Icons 的 Sparkle；E734 是 FavoriteStar（kFavorite），勿混用。
inline constexpr wchar_t kSparkle[] = L"\uEA3B";
inline constexpr wchar_t kCalendar[] = L"\uE787";
inline constexpr wchar_t kClock[] = L"\uE823";
inline constexpr wchar_t kPin[] = L"\uE718";
inline constexpr wchar_t kPlay[] = L"\uE768";
inline constexpr wchar_t kPause[] = L"\uE769";
inline constexpr wchar_t kView[] = L"\uE890";
inline constexpr wchar_t kHide[] = L"\uED1A";
inline constexpr wchar_t kEdit[] = L"\uE70F";
inline constexpr wchar_t kDownload[] = L"\uE896";
inline constexpr wchar_t kUpload[] = L"\uE898";
inline constexpr wchar_t kShare[] = L"\uE72D";
inline constexpr wchar_t kLink[] = L"\uE71B";
inline constexpr wchar_t kImage[] = L"\uE8B9";
inline constexpr wchar_t kFile[] = L"\uE8A5";
inline constexpr wchar_t kArrowLeft[] = L"\uE72B";
inline constexpr wchar_t kArrowRight[] = L"\uE72A";
inline constexpr wchar_t kArrowUp[] = L"\uE74A";
inline constexpr wchar_t kArrowDown[] = L"\uE74B";
inline constexpr wchar_t kMore[] = L"\uE712";
inline constexpr wchar_t kFilter[] = L"\uE71C";
inline constexpr wchar_t kMenu[] = L"\uE700";
inline constexpr wchar_t kList[] = L"\uE8FD";
inline constexpr wchar_t kRows[] = L"\uE8A8";
inline constexpr wchar_t kLock[] = L"\uE72E";
inline constexpr wchar_t kUnlock[] = L"\uE785";
inline constexpr wchar_t kGlobe[] = L"\uE774";
inline constexpr wchar_t kCloud[] = L"\uE753";
inline constexpr wchar_t kPrint[] = L"\uE749";
inline constexpr wchar_t kUndo[] = L"\uE7A7";
inline constexpr wchar_t kRedo[] = L"\uE7A6";
inline constexpr wchar_t kChat[] = L"\uE8F2";
inline constexpr wchar_t kSend[] = L"\uE724";
inline constexpr wchar_t kAttach[] = L"\uE723";
inline constexpr wchar_t kHelp[] = L"\uE897";
inline constexpr wchar_t kTag[] = L"\uE8EC";
inline constexpr wchar_t kBookmark[] = L"\uE8A4";
inline constexpr wchar_t kFlag[] = L"\uE7C1";
inline constexpr wchar_t kLocation[] = L"\uE707";
inline constexpr wchar_t kPeople[] = L"\uE716";
inline constexpr wchar_t kCamera[] = L"\uE722";
inline constexpr wchar_t kVideo[] = L"\uE714";
inline constexpr wchar_t kVolume[] = L"\uE767";
inline constexpr wchar_t kWifi[] = L"\uE701";
inline constexpr wchar_t kPower[] = L"\uE7E8";
inline constexpr wchar_t kSun[] = L"\uE706";
inline constexpr wchar_t kMoon[] = L"\uE708";
inline constexpr wchar_t kTerminal[] = L"\uE756";
inline constexpr wchar_t kDatabase[] = L"\uE9F9";
inline constexpr wchar_t kChart[] = L"\uE9D2";
inline constexpr wchar_t kBug[] = L"\uEBE8";
inline constexpr wchar_t kHeart[] = L"\uEB51";
inline constexpr wchar_t kFolderOpen[] = L"\uE838";
inline constexpr wchar_t kExternalLink[] = L"\uE8A7";
inline constexpr wchar_t kSort[] = L"\uE8CB";
inline constexpr wchar_t kGrid[] = L"\uF0E2";
inline constexpr wchar_t kSliders[] = L"\uE9E9";
inline constexpr wchar_t kKeyboard[] = L"\uE765";
inline constexpr wchar_t kInbox[] = L"\uF41F";
inline constexpr wchar_t kSignOut[] = L"\uF3B1";

} // namespace lumen::icon

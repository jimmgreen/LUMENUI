#include "lumen/Strings.h"
#include <windows.h>

namespace lumen {

Strings Strings::ForSystem() {
    const LANGID lang = PRIMARYLANGID(GetUserDefaultUILanguage());
    if (lang == LANG_CHINESE) return Chinese();
    return English();
}

} // namespace lumen

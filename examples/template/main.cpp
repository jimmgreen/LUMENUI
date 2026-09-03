#include <lumen/lumen.h>
#include <lumen/Main.h>
int lumen_main(std::span<const std::wstring_view>) {
    return lumen::Run(L"LUMEN", [](lumen::Window& w) {
        w.Root().Add<lumen::Button>(L"Hello", lumen::ButtonKind::Primary);
    });
}

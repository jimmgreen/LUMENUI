cmake_minimum_required(VERSION 3.25)
project(fluentui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(/W4 /permissive- /utf-8 /EHsc)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

# Static library. Each control lives in its own translation unit, so the
# linker pulls only the objects the user actually references.
add_library(fluentui STATIC
    src/core/app.cpp
    src/core/renderer.cpp
    src/core/offscreen.cpp
    src/core/text_service.cpp
    src/core/painter.cpp
    src/core/theme.cpp
    src/core/control.cpp
    src/core/window_impl.cpp
    src/core/menu_window.cpp
    src/core/draw_tree.cpp
    src/controls/button.cpp
    src/controls/checkbox.cpp
    src/controls/radio_button.cpp
    src/controls/switch.cpp
    src/controls/text_box.cpp
    src/controls/slider.cpp
    src/controls/progress_bar.cpp
    src/controls/combo_box.cpp
    src/controls/dialog.cpp
    src/controls/list_view.cpp
    src/controls/tab_control.cpp
    src/controls/label.cpp
)
target_include_directories(fluentui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fluentui PUBLIC
    d3d11 d2d1 dwrite dxgi dcomp dwmapi shcore user32 gdi32 windowscodecs uuid)

add_executable(fluentui_gallery WIN32 examples/gallery/main.cpp)
target_link_libraries(fluentui_gallery PRIVATE fluentui)

add_executable(fluentui_visual_test tests/visual/main.cpp)
target_include_directories(fluentui_visual_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fluentui_visual_test PRIVATE fluentui)

add_executable(fluentui_perf_test tests/perf/main.cpp)
target_include_directories(fluentui_perf_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fluentui_perf_test PRIVATE fluentui)

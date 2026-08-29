cmake_minimum_required(VERSION 3.25)
project(fluentui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(/W4 /permissive- /utf-8 /EHsc)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

# ---- LumaText 文字渲染（可选；找到时启用，找不到时回退 DirectWrite）----
option(FLUENTUI_WITH_LUMATEXT "Use LumaText for text rendering" ON)
set(LUMATEXT_SOURCE_DIR "" CACHE PATH "Optional LumaText source tree for development builds")
if(FLUENTUI_WITH_LUMATEXT AND NOT LUMATEXT_SOURCE_DIR)
    # 与 pulse 相同的约定：默认探测同级 ../lumatext
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../lumatext/CMakeLists.txt")
        set(LUMATEXT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lumatext" CACHE PATH
            "Optional LumaText source tree for development builds" FORCE)
    endif()
endif()

set(FLUENTUI_LUMATEXT_TARGET "")
set(FLUENTUI_LUMATEXT_RUNTIME_TARGET "")
if(FLUENTUI_WITH_LUMATEXT)
    if(LUMATEXT_SOURCE_DIR)
        if(NOT EXISTS "${LUMATEXT_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "LUMATEXT_SOURCE_DIR does not contain CMakeLists.txt")
        endif()
        set(LUMATEXT_BUILD_STATIC OFF)
        set(LUMATEXT_BUILD_SAMPLES OFF)
        set(LUMATEXT_BUILD_TESTS OFF)
        set(LUMATEXT_BUILD_REFERENCE_TOOLS OFF)
        add_subdirectory("${LUMATEXT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/_deps/lumatext" EXCLUDE_FROM_ALL)
    else()
        find_package(LumaText 0.1 CONFIG QUIET)
    endif()

    if(TARGET lumatext_shared)
        set(FLUENTUI_LUMATEXT_TARGET LumaText::D2D)
        set(FLUENTUI_LUMATEXT_RUNTIME_TARGET lumatext_shared)
        message(STATUS "fluentui text renderer: LumaText shared library")
    elseif(TARGET LumaText::D2D)
        set(FLUENTUI_LUMATEXT_TARGET LumaText::D2D)
        message(STATUS "fluentui text renderer: LumaText (prebuilt)")
    else()
        message(STATUS "fluentui text renderer: DirectWrite (LumaText was not found)")
    endif()
endif()

# 把 LumaText 桥接实现编进 target；is_exe=TRUE 时额外拷贝 DLL 到输出目录。
function(fluentui_enable_lumatext target is_exe)
    target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core/lumatext_bridge.cpp)
    if(FLUENTUI_LUMATEXT_TARGET)
        target_compile_definitions(${target} PRIVATE FLUENTUI_HAS_LUMATEXT=1)
        target_link_libraries(${target} PRIVATE ${FLUENTUI_LUMATEXT_TARGET})
        if(is_exe AND FLUENTUI_LUMATEXT_RUNTIME_TARGET)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${FLUENTUI_LUMATEXT_RUNTIME_TARGET}>
                    $<TARGET_FILE_DIR:${target}>)
            if(LUMATEXT_SOURCE_DIR)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        $<TARGET_FILE_DIR:${target}>/licenses/LumaText
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${LUMATEXT_SOURCE_DIR}/LICENSE"
                        $<TARGET_FILE_DIR:${target}>/licenses/LumaText/LumaText.txt
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${LUMATEXT_SOURCE_DIR}/LICENSES"
                        $<TARGET_FILE_DIR:${target}>/licenses/LumaText)
            endif()
        endif()
    endif()
endfunction()

# ---- 静态库：每个控件独立编译单元，链接器只拉用到的控件 ----
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
fluentui_enable_lumatext(fluentui FALSE)

add_executable(fluentui_gallery WIN32 examples/gallery/main.cpp)
target_link_libraries(fluentui_gallery PRIVATE fluentui)
fluentui_enable_lumatext(fluentui_gallery TRUE)

add_executable(fluentui_visual_test tests/visual/main.cpp)
target_include_directories(fluentui_visual_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fluentui_visual_test PRIVATE fluentui)
fluentui_enable_lumatext(fluentui_visual_test TRUE)

add_executable(fluentui_perf_test tests/perf/main.cpp)
target_include_directories(fluentui_perf_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(fluentui_perf_test PRIVATE fluentui)
fluentui_enable_lumatext(fluentui_perf_test TRUE)

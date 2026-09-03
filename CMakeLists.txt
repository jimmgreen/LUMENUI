cmake_minimum_required(VERSION 3.25)
project(lumen VERSION 0.1.0 LANGUAGES CXX RC)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(/W4 /permissive- /utf-8 /EHsc)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

# ---- LumaText 文字渲染（可选；找到时启用，找不到时回退 DirectWrite）----
option(LUMEN_WITH_LUMATEXT "Use LumaText for text rendering" ON)
option(LUMEN_REQUIRE_LUMATEXT "Fail configure if LumaText is missing" OFF)
set(LUMATEXT_SOURCE_DIR "" CACHE PATH "Optional LumaText source tree for development builds")
if(LUMEN_WITH_LUMATEXT AND NOT LUMATEXT_SOURCE_DIR AND PROJECT_IS_TOP_LEVEL)
    # 与 pulse 相同的约定：默认探测同级 ../lumatext
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../lumatext/CMakeLists.txt")
        set(LUMATEXT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lumatext" CACHE PATH
            "Optional LumaText source tree for development builds" FORCE)
    endif()
endif()

set(LUMEN_LUMATEXT_TARGET "")
set(LUMEN_LUMATEXT_RUNTIME_TARGET "")
if(LUMEN_WITH_LUMATEXT)
    if(LUMATEXT_SOURCE_DIR)
        if(NOT EXISTS "${LUMATEXT_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "LUMATEXT_SOURCE_DIR does not contain CMakeLists.txt")
        endif()
        set(LUMATEXT_BUILD_STATIC OFF)
        set(LUMATEXT_BUILD_SAMPLES OFF)
        set(LUMATEXT_BUILD_TESTS OFF)
        set(LUMATEXT_BUILD_REFERENCE_TOOLS OFF)
        # 复用 sibling lumatext 已解压的 HarfBuzz/FreeType，避免 Configure 再去拉 GitHub。
        foreach(_dep IN ITEMS harfbuzz freetype)
            string(TOUPPER "${_dep}" _DEP)
            if(NOT FETCHCONTENT_SOURCE_DIR_${_DEP})
                foreach(_candidate IN ITEMS
                    "${LUMATEXT_SOURCE_DIR}/build-vs18/_deps/${_dep}-src"
                    "${LUMATEXT_SOURCE_DIR}/build/_deps/${_dep}-src")
                    if(EXISTS "${_candidate}")
                        set(FETCHCONTENT_SOURCE_DIR_${_DEP} "${_candidate}" CACHE PATH
                            "Pre-extracted ${_dep} source for FetchContent" FORCE)
                        break()
                    endif()
                endforeach()
            endif()
        endforeach()
        # 子目录自带 LumaText 的 install()；SDK 只收 dll + 导入库，不把 LumaText 头文件铺进 prefix。
        set(_lumen_skip_install "${CMAKE_SKIP_INSTALL_RULES}")
        set(CMAKE_SKIP_INSTALL_RULES ON)
        add_subdirectory("${LUMATEXT_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/_deps/lumatext" EXCLUDE_FROM_ALL)
        set(CMAKE_SKIP_INSTALL_RULES "${_lumen_skip_install}")
    else()
        find_package(LumaText 0.1 CONFIG QUIET)
    endif()

    if(TARGET lumatext_shared)
        set(LUMEN_LUMATEXT_TARGET LumaText::D2D)
        set(LUMEN_LUMATEXT_RUNTIME_TARGET lumatext_shared)
        message(STATUS "lumen text renderer: LumaText shared library")
    elseif(TARGET LumaText::D2D)
        set(LUMEN_LUMATEXT_TARGET LumaText::D2D)
        message(STATUS "lumen text renderer: LumaText (prebuilt)")
    else()
        if(LUMEN_REQUIRE_LUMATEXT)
            message(FATAL_ERROR "LumaText not found")
        else()
            message(WARNING "LumaText not found; text falls back to DirectWrite")
        endif()
    endif()
endif()

# 把 LumaText 桥接实现编进 target；is_exe=TRUE 时额外拷贝 DLL 到输出目录。
function(lumen_enable_lumatext target is_exe)
    if(NOT is_exe)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core/lumatext_bridge.cpp)
    endif()
    if(LUMEN_LUMATEXT_TARGET)
        target_compile_definitions(${target} PRIVATE LUMEN_HAS_LUMATEXT=1)
        target_link_libraries(${target} PRIVATE ${LUMEN_LUMATEXT_TARGET})
    endif()
    if(is_exe)
        get_property(_lumen_rt TARGET lumen PROPERTY LUMEN_LUMATEXT_RUNTIME)
        if(_lumen_rt)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${_lumen_rt}>
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

# 给外部可执行文件拷贝 lumatext.dll（与库内测试/Gallery 同一套）。
function(lumen_copy_runtime target)
    lumen_enable_lumatext(${target} TRUE)
endfunction()

# WIN32 + lumen::lumen/lumen::main + 运行时 DLL + /utf-8。
function(lumen_add_executable name)
    add_executable(${name} WIN32 ${ARGN})
    target_link_libraries(${name} PRIVATE lumen::lumen lumen::main)
    lumen_copy_runtime(${name})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /permissive-)
        target_compile_definitions(${name} PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    endif()
endfunction()

if(PROJECT_IS_TOP_LEVEL)
    set(LUMEN_DEFAULT_APPS ON)
else()
    set(LUMEN_DEFAULT_APPS OFF)
endif()
option(LUMEN_BUILD_EXAMPLES "Build gallery" ${LUMEN_DEFAULT_APPS})
option(LUMEN_BUILD_TESTS "Build visual/perf/anim/api tests" ${LUMEN_DEFAULT_APPS})

# ---- 静态库：每个控件独立编译单元，链接器只拉用到的控件 ----
add_library(lumen STATIC
    src/core/app.cpp
    src/core/strings.cpp
    src/core/renderer.cpp
    src/core/offscreen.cpp
    src/core/text_service.cpp
    src/core/painter.cpp
    src/core/icon_path.cpp
    src/core/theme.cpp
    src/core/animate.cpp
    src/core/log.cpp
    src/core/control.cpp
    src/core/layout.cpp
    src/core/window_impl.cpp
    src/core/input_router.cpp
    src/core/overlay_host.cpp
    src/core/frame_chrome.cpp
    src/core/ime_bridge.cpp
    src/core/tray_host.cpp
    src/core/timer_host.cpp
    src/core/uia.cpp
    src/core/menu_window.cpp
    src/core/draw_tree.cpp
    src/controls/button.cpp
    src/controls/repeat_button.cpp
    src/controls/checkbox.cpp
    src/controls/toggle_button.cpp
    src/controls/radio_button.cpp
    src/controls/switch.cpp
    src/controls/text_box.cpp
    src/controls/slider.cpp
    src/controls/range_slider.cpp
    src/controls/progress_bar.cpp
    src/controls/combo_box.cpp
    src/controls/dialog.cpp
    src/controls/list_view.cpp
    src/controls/table.cpp
    src/controls/tab_control.cpp
    src/controls/label.cpp
    src/controls/split_button.cpp
    src/controls/drop_down_button.cpp
    src/controls/segmented.cpp
    src/controls/badge.cpp
    src/controls/info_badge.cpp
    src/controls/chip.cpp
    src/controls/token_box.cpp
    src/controls/icon_view.cpp
    src/controls/swatch.cpp
    src/controls/progress_ring.cpp
    src/controls/settings_card.cpp
    src/controls/expander.cpp
    src/controls/scroll_viewer.cpp
    src/controls/info_bar.cpp
    src/controls/hyperlink_button.cpp
    src/controls/separator.cpp
    src/controls/password_box.cpp
    src/controls/skeleton.cpp
    src/controls/rating.cpp
    src/controls/avatar.cpp
    src/controls/number_box.cpp
    src/controls/auto_suggest_box.cpp
    src/controls/flyout.cpp
    src/controls/teaching_tip.cpp
    src/controls/tool_tip.cpp
    src/controls/breadcrumb.cpp
    src/controls/tree_view.cpp
    src/controls/tree_table.cpp
    src/controls/split_view.cpp
    src/controls/splitter.cpp
    src/controls/title_bar.cpp
    src/controls/menu_bar.cpp
    src/controls/pagination.cpp
    src/controls/empty_state.cpp
    src/controls/file_drop_zone.cpp
    src/controls/form_field.cpp
    src/controls/status_bar.cpp
    src/controls/hotkey_box.cpp
    src/controls/group_box.cpp
    src/controls/view_box.cpp
    src/controls/carousel.cpp
    src/controls/stepper.cpp
    src/controls/date_picker.cpp
    src/controls/time_picker.cpp
    src/controls/image_view.cpp
    src/controls/command_bar.cpp
    src/controls/navigation_view.cpp
    src/controls/page_host.cpp
    src/controls/color_picker.cpp
    src/controls/calendar_view.cpp
    src/controls/grid_view.cpp
    src/core/clipboard.cpp
    src/core/dialogs.cpp
    src/core/hotkey.cpp
    src/core/shell.cpp
    src/core/settings.cpp
    src/controls/sparkline.cpp
    src/controls/gauge.cpp
    src/controls/chart.cpp
    src/controls/log_view.cpp
    src/controls/rich_label.cpp
    src/controls/busy_overlay.cpp
    src/controls/drawer.cpp
)
target_include_directories(lumen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(lumen PUBLIC cxx_std_20)
target_compile_definitions(lumen PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
if(MSVC)
    target_compile_options(lumen PUBLIC /permissive- /utf-8)
endif()
target_link_libraries(lumen PUBLIC
    d3d11 d2d1 dwrite dxgi dcomp dwmapi psapi shcore user32 gdi32 windowscodecs uuid imm32 ole32 oleaut32 uiautomationcore shell32 advapi32)
lumen_enable_lumatext(lumen FALSE)
if(LUMEN_LUMATEXT_RUNTIME_TARGET)
    set_property(TARGET lumen PROPERTY LUMEN_LUMATEXT_RUNTIME ${LUMEN_LUMATEXT_RUNTIME_TARGET})
endif()
add_library(lumen::lumen ALIAS lumen)

add_library(lumen_main STATIC src/core/main_entry.cpp)
add_library(lumen::main ALIAS lumen_main)
set_target_properties(lumen_main PROPERTIES EXPORT_NAME main)
target_include_directories(lumen_main PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(lumen_main PUBLIC lumen)
target_link_options(lumen_main INTERFACE "/INCLUDE:wWinMain" "/ENTRY:wWinMainCRTStartup")

file(GLOB LUMEN_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/lumen/*.h")

if(LUMEN_BUILD_EXAMPLES)
    add_executable(lumen_gallery WIN32
        examples/gallery/main.cpp
        examples/gallery/common.cpp
        examples/gallery/section_overview.cpp
        examples/gallery/section_buttons.cpp
        examples/gallery/section_input.cpp
        examples/gallery/section_selection.cpp
        examples/gallery/section_layout.cpp
        examples/gallery/section_collections.cpp
        examples/gallery/section_navigation.cpp
        examples/gallery/section_overlays.cpp
        examples/gallery/section_status.cpp
        examples/gallery/section_charts.cpp
        examples/gallery/gallery.rc)
    target_include_directories(lumen_gallery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(lumen_gallery PRIVATE lumen::lumen lumen::main)
    lumen_copy_runtime(lumen_gallery)
    set_property(SOURCE
        examples/gallery/main.cpp
        examples/gallery/common.cpp
        examples/gallery/section_overview.cpp
        examples/gallery/section_buttons.cpp
        examples/gallery/section_input.cpp
        examples/gallery/section_selection.cpp
        examples/gallery/section_layout.cpp
        examples/gallery/section_collections.cpp
        examples/gallery/section_navigation.cpp
        examples/gallery/section_overlays.cpp
        examples/gallery/section_status.cpp
        examples/gallery/section_charts.cpp
        APPEND PROPERTY OBJECT_DEPENDS ${LUMEN_PUBLIC_HEADERS})
endif()

if(LUMEN_BUILD_TESTS)
    add_executable(lumen_visual_test tests/visual/main.cpp)
    target_include_directories(lumen_visual_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(lumen_visual_test PRIVATE lumen)
    lumen_copy_runtime(lumen_visual_test)

    add_executable(lumen_perf_test tests/perf/main.cpp)
    target_include_directories(lumen_perf_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(lumen_perf_test PRIVATE lumen)
    lumen_copy_runtime(lumen_perf_test)

    add_executable(lumen_anim_test tests/anim/main.cpp)
    target_include_directories(lumen_anim_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(lumen_anim_test PRIVATE lumen)
    lumen_copy_runtime(lumen_anim_test)

    add_executable(lumen_api_test tests/api/chain_compile.cpp)
    target_link_libraries(lumen_api_test PRIVATE lumen)
    add_custom_command(TARGET lumen_api_test POST_BUILD
        COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/naming.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Check public API naming")

    set(LUMEN_README_GEN ${CMAKE_CURRENT_BINARY_DIR}/readme_generated.cpp)
    add_custom_command(OUTPUT ${LUMEN_README_GEN}
        COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/extract_readme.py
            ${CMAKE_CURRENT_SOURCE_DIR}/README.md ${LUMEN_README_GEN}
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/README.md
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/api/extract_readme.py
        COMMENT "Extract README C++ snippets")
    add_library(lumen_readme_check OBJECT ${LUMEN_README_GEN})
    target_link_libraries(lumen_readme_check PRIVATE lumen)
    add_dependencies(lumen_api_test lumen_readme_check)

    set_property(SOURCE
        tests/visual/main.cpp
        tests/perf/main.cpp
        tests/anim/main.cpp
        tests/api/chain_compile.cpp
        APPEND PROPERTY OBJECT_DEPENDS ${LUMEN_PUBLIC_HEADERS})
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS lumen lumen_main
    EXPORT lumenTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/lumen DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(LUMEN_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/lumen)
install(EXPORT lumenTargets
    FILE lumenTargets.cmake
    NAMESPACE lumen::
    DESTINATION ${LUMEN_CMAKE_INSTALL_DIR})
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lumenConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/lumenConfig.cmake
    INSTALL_DESTINATION ${LUMEN_CMAKE_INSTALL_DIR})
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/lumenConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lumenConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/lumenConfigVersion.cmake
    DESTINATION ${LUMEN_CMAKE_INSTALL_DIR})

install(FILES LICENSE DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk-readme.txt DESTINATION . RENAME README.txt)
if(LUMEN_LUMATEXT_RUNTIME_TARGET)
    install(FILES $<TARGET_FILE:${LUMEN_LUMATEXT_RUNTIME_TARGET}>
        DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES $<TARGET_LINKER_FILE:${LUMEN_LUMATEXT_RUNTIME_TARGET}>
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(LUMATEXT_SOURCE_DIR AND EXISTS "${LUMATEXT_SOURCE_DIR}/LICENSE")
    install(FILES "${LUMATEXT_SOURCE_DIR}/LICENSE"
        DESTINATION licenses/LumaText
        RENAME LumaText.txt)
    if(EXISTS "${LUMATEXT_SOURCE_DIR}/LICENSES")
        install(DIRECTORY "${LUMATEXT_SOURCE_DIR}/LICENSES/"
            DESTINATION licenses/LumaText)
    endif()
endif()


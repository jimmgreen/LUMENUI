# LUMEN 发布用预编译导出：只含共享库，路径相对本文件，包可重定位。
get_filename_component(_lmt_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT TARGET LumaText::Shared)
    add_library(LumaText::Shared SHARED IMPORTED GLOBAL)
    set_target_properties(LumaText::Shared PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_lmt_prefix}/include"
        IMPORTED_LOCATION "${_lmt_prefix}/bin/lumatext.dll"
        IMPORTED_IMPLIB "${_lmt_prefix}/lib/lumatext.lib")
    set_property(TARGET LumaText::Shared APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
    set_target_properties(LumaText::Shared PROPERTIES
        IMPORTED_LOCATION_RELEASE "${_lmt_prefix}/bin/lumatext.dll"
        IMPORTED_IMPLIB_RELEASE "${_lmt_prefix}/lib/lumatext.lib")
endif()

unset(_lmt_prefix)

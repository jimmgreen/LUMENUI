# 源码接入和安装 SDK 共用；运行时属性挂在库目标上，不依赖调用方目录变量。
function(lumen_copy_runtime target)
    get_target_property(_type ${target} TYPE)
    if(NOT _type MATCHES "^(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)$")
        message(FATAL_ERROR "lumen_copy_runtime requires an executable, DLL or module target")
    endif()
    get_property(_copied TARGET ${target} PROPERTY LUMEN_RUNTIME_COPY_ADDED)
    if(_copied)
        return()
    endif()
    get_property(_runtime TARGET lumen::lumen PROPERTY LUMEN_LUMATEXT_RUNTIME)
    if(NOT _runtime)
        return()
    endif()
    set_property(TARGET ${target} PROPERTY LUMEN_RUNTIME_COPY_ADDED TRUE)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${_runtime}>" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    get_property(_licenses TARGET lumen::lumen PROPERTY LUMEN_RUNTIME_LICENSES)
    if(_licenses)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_licenses}" "$<TARGET_FILE_DIR:${target}>/licenses/LumaText"
            VERBATIM)
    endif()
endfunction()

function(lumen_add_executable name)
    add_executable(${name} WIN32 ${ARGN})
    target_link_libraries(${name} PRIVATE lumen::lumen lumen::main)
    lumen_copy_runtime(${name})
endfunction()

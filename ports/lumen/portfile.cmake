get_filename_component(LUMEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
vcpkg_cmake_configure(
    SOURCE_PATH "${LUMEN_ROOT}"
    OPTIONS
        -DLUMEN_BUILD_EXAMPLES=OFF
        -DLUMEN_BUILD_TESTS=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME lumen)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
if(EXISTS "${LUMEN_ROOT}/LICENSE")
    file(INSTALL "${LUMEN_ROOT}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
else()
    file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" "See the LUMEN repository license.")
endif()

cmake_minimum_required(VERSION 3.24)
project(ShipwrightRALinkSmoke C)

# Use the exact application dependency target and flags. This also exercises
# the static MSVC runtime setting when configured with a Windows toolchain.
include("${CMAKE_CURRENT_LIST_DIR}/../../../soh/CMake/GlobalSettingsInclude.cmake")
add_executable(ra-link-smoke ../link_smoke.c)
if(MSVC)
    set_target_properties(ra-link-smoke PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()
enable_testing()
add_test(NAME ra-link-smoke COMMAND ra-link-smoke)

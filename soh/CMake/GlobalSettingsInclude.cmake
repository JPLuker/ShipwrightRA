# ShipwrightRA project-local CMake extensions.
#
# Shipwright's soh/CMakeLists.txt intentionally loads this file when present,
# which lets the RetroAchievements experiment remain isolated from upstream
# build files and makes future upstream rebases substantially less invasive.

include(FetchContent)

set(SHIPWRIGHT_RA_RCHEEVOS_TAG "v12.5.0" CACHE STRING
    "Pinned rcheevos release used by ShipwrightRA")

FetchContent_Declare(
    shipwright_ra_rcheevos_source
    GIT_REPOSITORY https://github.com/RetroAchievements/rcheevos.git
    GIT_TAG ${SHIPWRIGHT_RA_RCHEEVOS_TAG}
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(shipwright_ra_rcheevos_source)
if(NOT shipwright_ra_rcheevos_source_POPULATED)
    FetchContent_Populate(shipwright_ra_rcheevos_source)
endif()

# rcheevos does not ship a CMake project. Mirror the source selection used by
# its official Swift package: src, rcheevos, rapi and rhash, excluding the
# libretro/external-client translation units.
file(GLOB SHIPWRIGHT_RA_RCHEEVOS_SOURCES CONFIGURE_DEPENDS
    "${shipwright_ra_rcheevos_source_SOURCE_DIR}/src/*.c"
    "${shipwright_ra_rcheevos_source_SOURCE_DIR}/src/rcheevos/*.c"
    "${shipwright_ra_rcheevos_source_SOURCE_DIR}/src/rapi/*.c"
    "${shipwright_ra_rcheevos_source_SOURCE_DIR}/src/rhash/*.c"
)

list(REMOVE_ITEM SHIPWRIGHT_RA_RCHEEVOS_SOURCES
    "${shipwright_ra_rcheevos_source_SOURCE_DIR}/src/rc_libretro.c"
    "${shipwright_ra_rcheevos_source_SOURCE_DIR}/src/rc_client_external.c"
)

add_library(shipwright_ra_rcheevos STATIC ${SHIPWRIGHT_RA_RCHEEVOS_SOURCES})
target_include_directories(shipwright_ra_rcheevos PUBLIC
    "${shipwright_ra_rcheevos_source_SOURCE_DIR}/include"
)
target_compile_definitions(shipwright_ra_rcheevos
    PRIVATE
        RC_DISABLE_LUA
    PUBLIC
        # Consumers need this definition too so rc_client.h exposes the
        # identify-and-load hashing API used by the upcoming ROM detection phase.
        RC_CLIENT_SUPPORTS_HASH
)
set_target_properties(shipwright_ra_rcheevos PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# GlobalSettingsInclude.cmake is evaluated before the soh target is created.
# link_libraries applies dependencies to targets created below this point
# without requiring changes to the upstream soh/CMakeLists.txt.
link_libraries(shipwright_ra_rcheevos)

# RetroAchievements API traffic requires HTTPS. Keep the transport dependency
# limited to desktop builds; unsupported console builds retain the explicit
# client-error fallback in RetroAchievementsManager::ServerCall.
if(NOT CMAKE_SYSTEM_NAME MATCHES "NintendoSwitch|CafeOS")
    # The top-level Windows build owns vcpkg bootstrap. Reuse that helper when
    # available so CURL::libcurl is provisioned without touching upstream CMake.
    if(WIN32 AND COMMAND vcpkg_install_packages)
        vcpkg_install_packages(curl)
    endif()

    find_package(CURL REQUIRED)
    link_libraries(CURL::libcurl)
    add_compile_definitions(SHIPWRIGHT_RA_HTTP_CURL=1)
endif()

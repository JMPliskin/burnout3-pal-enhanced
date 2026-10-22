# Minimal PAL bootstrap, based on engine/templates/game/port/CMakeLists.txt.
cmake_minimum_required(VERSION 3.20)
project(burnout3_pal_port LANGUAGES CXX RC)
set(PS2_GENERATED_DIR "" CACHE PATH "dispatch.cpp from engine/tools/port.py")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    get_filename_component(repo_root ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
    file(TO_NATIVE_PATH "${repo_root}/" repo_root_native)
    add_compile_options("/d1trimfile:${repo_root_native}")
endif()
# Keep the host's standard backend: its current software-only branch lacks
# four definitions at link time. No NTSC-U graphics hooks are included.
option(PS2HOST_VULKAN "Build the shared Vulkan backend" ON)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../engine/host ${CMAKE_BINARY_DIR}/ps2host)
set(PS2_PAL_SOURCES_FILE "" CACHE FILEPATH "Reviewed PAL translation list; avoids modifying historical generated units")
set(pal_extra_sources)
if(PS2_PAL_SOURCES_FILE)
    file(STRINGS "${PS2_PAL_SOURCES_FILE}" pal_extra_sources)
    foreach(pal_source IN LISTS pal_extra_sources)
        if(NOT EXISTS "${pal_source}")
            message(FATAL_ERROR "Missing PAL source: ${pal_source}")
        endif()
    endforeach()
endif()
set(pal_translated ${CMAKE_CURRENT_LIST_DIR}/../src/lifted)
if(EXISTS "${PS2_GENERATED_DIR}/pal_sources.txt")
    file(READ "${PS2_GENERATED_DIR}/pal_sources.txt" pal_translated)
    string(STRIP "${pal_translated}" pal_translated)
endif()
if(PS2_PAL_SOURCES_FILE)
    set(pal_translated "${CMAKE_BINARY_DIR}/empty-translated")
    file(MAKE_DIRECTORY "${pal_translated}")
endif()
ps2_add_port(burnout3-pal
    GAME ${CMAKE_CURRENT_LIST_DIR}/..
    GENERATED ${PS2_GENERATED_DIR}
    TRANSLATED ${pal_translated}
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/overrides.cpp
            ${CMAKE_CURRENT_LIST_DIR}/widescreen_camera.cpp
            ${CMAKE_CURRENT_LIST_DIR}/widescreen_hud.cpp ${pal_extra_sources})
if(EXISTS "${PS2_GENERATED_DIR}/relocation_audit.cpp")
    target_compile_definitions(burnout3-pal PRIVATE PAL_RELOCATION_AUDIT)
endif()

# PAL Unicode player entry point reuses the shared host and disc/settings APIs.
get_target_property(pal_target_sources burnout3-pal SOURCES)
list(FILTER pal_target_sources EXCLUDE REGEX "engine/host/src/main[.]cpp$")
set_property(TARGET burnout3-pal PROPERTY SOURCES "${pal_target_sources}")
target_sources(burnout3-pal PRIVATE ${CMAKE_CURRENT_LIST_DIR}/windows_main.cpp)
if(MSVC)
    target_link_options(burnout3-pal PRIVATE "/MANIFESTINPUT:${CMAKE_CURRENT_LIST_DIR}/windows.manifest")
endif()
set_target_properties(burnout3-pal PROPERTIES OUTPUT_NAME "Burnout3-PAL-Enhanced")

# Version metadata without bundling an icon derived from the game disc.
# If an explicit local icon exists, ps2_add_port already supplies VERSIONINFO.
if(WIN32 AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../build/port/icon.ico"
         AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/icon.ico")
    file(READ "${CMAKE_CURRENT_LIST_DIR}/../game.json" pal_game_json)
    string(JSON PAL_PLAYER_VERSION GET ${pal_game_json} version)
    string(REGEX MATCHALL "[0-9]+" pal_version_numbers "${PAL_PLAYER_VERSION}")
    list(APPEND pal_version_numbers 0 0 0 0)
    list(SUBLIST pal_version_numbers 0 4 pal_version_numbers)
    string(REPLACE ";" "," PAL_PLAYER_FIXED_VERSION "${pal_version_numbers}")
    configure_file(windows_version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/pal_player_version.rc @ONLY)
    target_sources(burnout3-pal PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/pal_player_version.rc)
endif()

add_executable(pal_player_startup_tests tests/player_startup_tests.cpp)
target_link_libraries(pal_player_startup_tests PRIVATE ps2host)
target_compile_features(pal_player_startup_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(pal_player_startup_tests PRIVATE /UNDEBUG)
    target_link_options(pal_player_startup_tests PRIVATE "/MANIFESTINPUT:${CMAKE_CURRENT_LIST_DIR}/windows.manifest")
endif()

# SDL assertions must not embed the developer's dependency-cache location.
if(MSVC AND TARGET SDL3-static)
    get_target_property(pal_sdl_source SDL3-static SOURCE_DIR)
    file(TO_NATIVE_PATH "${pal_sdl_source}/" pal_sdl_native)
    target_compile_options(SDL3-static PRIVATE "/d1trimfile:${pal_sdl_native}")
endif()

# Optional regression executables share only objects from this same build.
# Existing translated inputs are required; no EE/VU generator is invoked.
option(PAL_WIDESCREEN_TESTS "Build PAL camera and HUD regression tests" OFF)
if(PAL_WIDESCREEN_TESTS)
    get_target_property(pal_wide_sources burnout3-pal SOURCES)
    set(pal_wide_core ${pal_wide_sources})
    list(FILTER pal_wide_core EXCLUDE REGEX "(windows_main|widescreen_hud)[.]cpp$|[.]rc$")
    add_library(pal_widescreen_core OBJECT ${pal_wide_core})
    target_link_libraries(pal_widescreen_core PRIVATE ps2host)
    get_target_property(pal_wide_definitions burnout3-pal COMPILE_DEFINITIONS)
    target_compile_definitions(pal_widescreen_core PRIVATE ${pal_wide_definitions})
    list(REMOVE_ITEM pal_wide_sources ${pal_wide_core})
    set_property(TARGET burnout3-pal PROPERTY SOURCES "${pal_wide_sources};$<TARGET_OBJECTS:pal_widescreen_core>")
    foreach(kind camera hud)
        add_executable(pal_widescreen_${kind}_tests
            tests/widescreen_${kind}_tests.cpp $<TARGET_OBJECTS:pal_widescreen_core>)
        if(kind STREQUAL "camera")
            target_sources(pal_widescreen_${kind}_tests PRIVATE widescreen_hud.cpp)
        endif()
        target_link_libraries(pal_widescreen_${kind}_tests PRIVATE ps2host)
        if(MSVC)
            target_compile_options(pal_widescreen_${kind}_tests PRIVATE /UNDEBUG)
            target_link_libraries(pal_widescreen_${kind}_tests PRIVATE ps2_cpu_check)
            target_link_options(pal_widescreen_${kind}_tests PRIVATE
                "/MANIFESTINPUT:${CMAKE_CURRENT_LIST_DIR}/windows.manifest")
        endif()
    endforeach()
endif()

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
set(pal_translated ${CMAKE_CURRENT_LIST_DIR}/../src/lifted)
if(EXISTS "${PS2_GENERATED_DIR}/pal_sources.txt")
    file(READ "${PS2_GENERATED_DIR}/pal_sources.txt" pal_translated)
    string(STRIP "${pal_translated}" pal_translated)
endif()
ps2_add_port(burnout3-pal
    GAME ${CMAKE_CURRENT_LIST_DIR}/..
    GENERATED ${PS2_GENERATED_DIR}
    TRANSLATED ${pal_translated}
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/overrides.cpp)

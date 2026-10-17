# Burnout 3: Takedown, native. Build with engine/tools/port.py, which generates
# the dispatch table and drives this file.

cmake_minimum_required(VERSION 3.20)
project(burnout3_port LANGUAGES CXX RC)     # RC: the icon

set(PS2_GENERATED_DIR "" CACHE PATH "dispatch.cpp, from engine/tools/port.py")

# The C and C++ runtime linked in (/MT): the executable runs on a Windows
# without the Visual C++ redistributable - every library here built so too.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Source files' names (__FILE__: SDL's assertions) from the repository's root,
# not the folder it was built in: a release names no one's folders.
if(MSVC)
    get_filename_component(repo_root ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
    file(TO_NATIVE_PATH "${repo_root}/" repo_root_native)
    add_compile_options("/d1trimfile:${repo_root_native}")
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../engine/host ${CMAKE_BINARY_DIR}/ps2host)

# PC SETTINGS' MODS: the functions the mods change, lifted with them
# (tools/lift_mods.py) from the player's executable - the game's code, so
# made here rather than kept in the repository.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MODS_LIFTED ${CMAKE_BINARY_DIR}/mods_lifted.cpp)
add_custom_command(OUTPUT ${MODS_LIFTED}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/lift_mods.py
            --game ${CMAKE_CURRENT_LIST_DIR}/.. --out ${MODS_LIFTED}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tools/lift_mods.py
    COMMENT "Lifting the mods' functions")

ps2_add_port(burnout3
    GAME ${CMAKE_CURRENT_LIST_DIR}/..
    GENERATED ${PS2_GENERATED_DIR}
    TRANSLATED ${CMAKE_CURRENT_LIST_DIR}/../src/lifted
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/overrides.cpp
            ${CMAKE_CURRENT_LIST_DIR}/iop_servers.cpp
            ${CMAKE_CURRENT_LIST_DIR}/widescreen.cpp
            ${CMAKE_CURRENT_LIST_DIR}/mouse.cpp
            ${CMAKE_CURRENT_LIST_DIR}/prompts.cpp
            ${CMAKE_CURRENT_LIST_DIR}/controls.cpp
            ${CMAKE_CURRENT_LIST_DIR}/sixty_fps.cpp
            ${CMAKE_CURRENT_LIST_DIR}/dev_menu.cpp
            ${CMAKE_CURRENT_LIST_DIR}/mods.cpp
            ${MODS_LIFTED})

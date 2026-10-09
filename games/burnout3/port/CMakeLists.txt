# Burnout 3: Takedown, native. Build with engine/tools/port.py, which generates
# the dispatch table and drives this file.

cmake_minimum_required(VERSION 3.20)
project(burnout3_port LANGUAGES CXX)

set(PS2_GENERATED_DIR "" CACHE PATH "dispatch.cpp, from engine/tools/port.py")

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../engine/host ${CMAKE_BINARY_DIR}/ps2host)

ps2_add_port(burnout3
    GENERATED ${PS2_GENERATED_DIR}
    TRANSLATED ${CMAKE_CURRENT_LIST_DIR}/../src/lifted
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/overrides.cpp
            ${CMAKE_CURRENT_LIST_DIR}/iop_servers.cpp
            ${CMAKE_CURRENT_LIST_DIR}/widescreen.cpp)

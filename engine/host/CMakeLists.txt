# The native host: the shared runtime (PS2 instruction semantics, also used by
# verification) and the PC platform layer that serves the translated code's
# hooks. A game port adds its translated functions with ps2_add_port().

cmake_minimum_required(VERSION 3.20)

set(PS2_ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

add_library(ps2rt STATIC
    ${PS2_ENGINE_DIR}/runtime/src/eefpu.cpp
    ${PS2_ENGINE_DIR}/runtime/src/eeint.cpp
    ${PS2_ENGINE_DIR}/runtime/src/vu0.cpp
    ${PS2_ENGINE_DIR}/runtime/src/lifted_support.cpp)
target_include_directories(ps2rt PUBLIC ${PS2_ENGINE_DIR}/runtime/include)
target_compile_features(ps2rt PUBLIC cxx_std_17)

add_library(ps2host STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/memory.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/kernel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/devices.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/threads.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/iop.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/sdk.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/disc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/cdvd.cpp)
target_include_directories(ps2host PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(ps2host PUBLIC ps2rt)

if(MSVC)
    # Translated code throws lifted::Abort; its chunks are large.
    target_compile_options(ps2rt PUBLIC /EHsc /bigobj /W3 /wd4100 /wd4189)
    target_compile_definitions(ps2rt PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

# The host's own tests, which need no game.
add_executable(ps2host_tests ${CMAKE_CURRENT_LIST_DIR}/tests/host_tests.cpp)
target_link_libraries(ps2host_tests PRIVATE ps2host)

# ps2_add_port(<target>
#     GENERATED <dir with dispatch.cpp from engine/tools/port.py>
#     TRANSLATED <dir with the game's translated sources (src/lifted)>
#     SOURCES <the game's own sources: overrides, enhancements>)
function(ps2_add_port target)
    cmake_parse_arguments(PORT "" "GENERATED;TRANSLATED" "SOURCES" ${ARGN})
    file(GLOB translated ${PORT_TRANSLATED}/*.cpp)
    # dispatch.cpp, and discovered.cpp: code found copied at run time, translated.
    file(GLOB generated ${PORT_GENERATED}/*.cpp)
    add_executable(${target}
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src/main.cpp
        ${generated}
        ${translated}
        ${PORT_SOURCES})
    target_link_libraries(${target} PRIVATE ps2host)
endfunction()

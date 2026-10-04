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
    ${CMAKE_CURRENT_LIST_DIR}/src/cdvd.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/fileio.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/vif.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dma.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/loadfile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/dbc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/mc2.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/iop_vm.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/iop_libs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/iopheap.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/spu2.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/audio.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/vu.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/timers.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/window.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gs_draw.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gs_memory.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gpu_bridge.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/profile.cpp)
# The IOP interpreter names library functions from engine/data/iop_exports.csv.
target_compile_definitions(ps2host PRIVATE PS2_ENGINE_DATA="${PS2_ENGINE_DIR}/data")
target_include_directories(ps2host PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(ps2host PUBLIC ps2rt)

# The window through SDL3 and Vulkan (engine/gpu), with GDI as the fallback.
option(PS2HOST_VULKAN "Present the picture through SDL3 and Vulkan" ON)
if(PS2HOST_VULKAN)
    add_subdirectory(${PS2_ENGINE_DIR}/gpu ${CMAKE_BINARY_DIR}/ps2gpu)
    target_link_libraries(ps2host PUBLIC ps2gpu)
    target_compile_definitions(ps2host PRIVATE PS2HOST_VULKAN=1)
endif()

if(MSVC)
    # Translated code throws lifted::Abort; its chunks are large.
    target_compile_options(ps2rt PUBLIC /EHsc /bigobj /W3 /wd4100 /wd4189)
    target_compile_definitions(ps2rt PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ps2host PRIVATE /Z7)     # symbols for --sample, as the ports'
    target_compile_options(ps2rt PRIVATE /Z7)
    target_link_libraries(ps2host PRIVATE dbghelp winmm)  # ...which resolves them; 1 ms sleeps
endif()

# The host's own tests, which need no game.
# vu_corpus.cpp: random microprograms, compiled by engine/tools/vurecomp.py
# --corpus as the build goes, which VuCompiledIsTheInterpreter runs both ways.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(PS2HOST_VURECOMP ${CMAKE_CURRENT_LIST_DIR}/../tools/vurecomp.py)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vu_corpus.cpp
    COMMAND ${Python3_EXECUTABLE} ${PS2HOST_VURECOMP} --corpus --count 120 --seed 1
            --out ${CMAKE_CURRENT_BINARY_DIR}/vu_corpus.cpp
    DEPENDS ${PS2HOST_VURECOMP}
    COMMENT "vurecomp: the VU test corpus")
add_executable(ps2host_tests ${CMAKE_CURRENT_LIST_DIR}/tests/host_tests.cpp
                             ${CMAKE_CURRENT_BINARY_DIR}/vu_corpus.cpp)
target_link_libraries(ps2host_tests PRIVATE ps2host)
if(PS2HOST_VULKAN)
    target_compile_definitions(ps2host_tests PRIVATE PS2HOST_VULKAN=1)
endif()
# The IOP interpreter's internals, for its instruction tests.
target_include_directories(ps2host_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

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
    if(MSVC)
        # Symbols in the optimised build too, for --sample (profile.cpp): /Z7
        # keeps them in the objects (no PDB contention in parallel builds),
        # /DEBUG gathers them; /OPT keeps what it does without symbols.
        target_compile_options(${target} PRIVATE /Z7)
        target_link_options(${target} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
endfunction()

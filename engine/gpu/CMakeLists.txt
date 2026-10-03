# The port's display on the GPU: SDL3 for the window, input and events, Vulkan
# for the picture. Vulkan reaches every platform the port aims at - Windows,
# Linux and Android natively, macOS through MoltenVK - and gives the control a GS
# renderer needs; SDL3 hides each platform's window and input. Both come from
# their releases at configure time (FetchContent), so nothing is installed on
# the machine and nothing of theirs is committed.
#
# Stage one presents the software GS's picture (a copy and a scaled blit, no
# shaders). The GPU GS renderer builds on this device and swapchain.

cmake_minimum_required(VERSION 3.20)
project(ps2gpu CXX C)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.363
    GIT_SHALLOW TRUE)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.4.363.0
    GIT_SHALLOW TRUE)
FetchContent_Declare(sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vulkan_headers)
set(VOLK_HEADERS_ONLY OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_INSTALL_DIR "${vulkan_headers_SOURCE_DIR}" CACHE PATH "" FORCE)
FetchContent_MakeAvailable(volk sdl3)

add_library(ps2gpu STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/vulkan_display.cpp)
target_include_directories(ps2gpu PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_features(ps2gpu PUBLIC cxx_std_17)
target_link_libraries(ps2gpu PUBLIC SDL3::SDL3-static PRIVATE volk::volk Vulkan::Headers)
if(MSVC)
    target_compile_options(ps2gpu PRIVATE /W3)
endif()

add_executable(ps2gpu_smoke ${CMAKE_CURRENT_LIST_DIR}/tests/display_smoke.cpp)
target_link_libraries(ps2gpu_smoke PRIVATE ps2gpu)

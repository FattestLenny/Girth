cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

project(girth)

find_package(OpenGL REQUIRED)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.8)

FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.8)

set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(portaudio GIT_REPOSITORY https://github.com/PortAudio/portaudio.git GIT_TAG v19.7.0)

FetchContent_MakeAvailable(SDL3 imgui portaudio)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_executable(girth ${SOURCES} ${IMGUI_SOURCES})

target_include_directories(girth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_features(girth PRIVATE cxx_std_23)
set_target_properties(girth PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(girth PRIVATE
    SDL3::SDL3
    OpenGL::GL
    portaudio_static
)

target_compile_options(girth PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /WX /permissive-
    >

    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wunused -Wno-error=unused-parameter -Wswitch-enum -pedantic-errors -Wno-error=switch-enum
    >
)

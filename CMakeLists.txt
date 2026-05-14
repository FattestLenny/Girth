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

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vst3sdk GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git GIT_TAG v3.8.0_build_66)

FetchContent_MakeAvailable(SDL3 imgui portaudio vst3sdk)

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

set(VST3_HOST_SOURCES
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
)

if(MSVC)
    set_source_files_properties(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        PROPERTIES COMPILE_FLAGS "/W0"
    )
    # module_win32.cpp must be compiled as C++17: in C++20+ mode MSVC doesn't
    # update __cplusplus without /Zc:__cplusplus, so SMTG_CPP20 stays 0 and
    # generic_u8string() returns std::u8string (char8_t), breaking the build.
    # C++17 keeps generic_u8string() returning std::string, no issue.
    set_source_files_properties(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
        PROPERTIES COMPILE_FLAGS "/W0 /std:c++17"
    )
endif()

add_executable(girth ${SOURCES} ${IMGUI_SOURCES} ${VST3_HOST_SOURCES})

target_include_directories(girth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_include_directories(girth SYSTEM PRIVATE
    ${vst3sdk_SOURCE_DIR}
)

target_compile_features(girth PRIVATE cxx_std_23)
set_target_properties(girth PROPERTIES CXX_EXTENSIONS OFF)

target_link_libraries(girth PRIVATE
    SDL3::SDL3
    OpenGL::GL
    portaudio_static
    sdk_hosting
)

target_compile_options(girth PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /WX /permissive-
    >

    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wunused -Wno-error=unused-parameter -Wswitch-enum -pedantic-errors -Wno-error=switch-enum
    >
)

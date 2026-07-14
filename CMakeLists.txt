cmake_minimum_required(VERSION 3.20)
project(nWiiRecomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.28.5
)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# SDL's Cocoa GL backend unconditionally asks for NSOpenGLPFAAllowOfflineRenderers
# *and* pins NSOpenGLPFAScreenMask to one display. On dual-GPU Macs (e.g. Intel +
# Radeon Pro) no pixel format satisfies both, so every SDL_GL_CreateContext fails
# with "Failed creating OpenGL pixel format" — verified with a standalone probe:
# the same attributes minus this one succeed. Drop it; offline renderers are of
# no use to us anyway. Idempotent, runs at configure time before SDL compiles.
if(APPLE)
    set(_sdl_gl_src "${sdl2_SOURCE_DIR}/src/video/cocoa/SDL_cocoaopengl.m")
    if(EXISTS "${_sdl_gl_src}")
        file(READ "${_sdl_gl_src}" _sdl_gl_txt)
        string(REPLACE "attr[i++] = NSOpenGLPFAAllowOfflineRenderers;"
                       "/* NWii: breaks pixel format on dual-GPU Macs */"
                       _sdl_gl_patched "${_sdl_gl_txt}")
        if(NOT _sdl_gl_txt STREQUAL _sdl_gl_patched)
            file(WRITE "${_sdl_gl_src}" "${_sdl_gl_patched}")
            message(STATUS "Patched SDL Cocoa GL: removed AllowOfflineRenderers")
        endif()
    endif()
endif()

add_subdirectory(nWiiAnalyzer)
add_subdirectory(nWiiRecomp)
add_subdirectory(nWiiRuntime)

# If nWiiStudio relies on external libs like raylib that aren't set up yet, 
# it might fail. Let's include it cautiously or just add it.
# add_subdirectory(nWiiStudio)
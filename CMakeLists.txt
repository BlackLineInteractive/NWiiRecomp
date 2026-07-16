cmake_minimum_required(VERSION 3.20)
project(nWiiRecomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Dolphin SDK signature database (GPLv2+ data from dolphin-emu, referenced by
# the config TOMLs as third_party/dolphin-sigdb/totaldb.dsy). Fetched on demand
# so the blob does not have to live in this repository.
set(NWII_SIGDB "${CMAKE_SOURCE_DIR}/third_party/dolphin-sigdb/totaldb.dsy")
if(NOT EXISTS "${NWII_SIGDB}")
    message(STATUS "totaldb.dsy not found - downloading from dolphin-emu")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Data/Sys/totaldb.dsy"
        "${NWII_SIGDB}"
        EXPECTED_HASH SHA256=09fb4a167946dbdfee5890c0f027e40b3eacbb6cc5ee9184adeb9d1a81c2c0aa
        STATUS NWII_SIGDB_STATUS
        SHOW_PROGRESS)
    list(GET NWII_SIGDB_STATUS 0 NWII_SIGDB_CODE)
    if(NOT NWII_SIGDB_CODE EQUAL 0)
        file(REMOVE "${NWII_SIGDB}")
        message(FATAL_ERROR
            "totaldb.dsy download failed (${NWII_SIGDB_STATUS}). Fetch it "
            "manually from the Dolphin repository (Data/Sys/totaldb.dsy) into "
            "third_party/dolphin-sigdb/. If upstream changed the file, update "
            "EXPECTED_HASH here.")
    endif()
endif()

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
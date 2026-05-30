# Project definition
cmake_minimum_required(VERSION 3.16)
project(nWiiRecomp LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Core Recompiler Library ---
# This library will contain the main logic: analyzer, decoder, recompiler, etc.
add_library(wiirecomp)

target_sources(wiirecomp
    PRIVATE
        # Core recompiler source files
        wiirecomp/src/loader/dol_analyzer.cpp
        wiirecomp/src/ppc/decoder.cpp
        wiirecomp/src/ppc/instruction.cpp
        wiirecomp/src/analyzer/code_analyzer.cpp
)

target_include_directories(wiirecomp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/wiirecomp/include
)

# --- Studio GUI Application ---
# This is the main user-facing tool.
add_executable(nWiiStudio)

target_sources(nWiiStudio
    PRIVATE
        nWiiStudio/src/main.cpp
        # We will add other UI-related source files here
)

target_include_directories(nWiiStudio
    PRIVATE
        nWiiStudio/include
        # We'll need to add paths to ImGui, Raylib, etc. here
        # external/raylib/src
        # external/imgui
)

# Link the Studio app against our core library
target_link_libraries(nWiiStudio
    PRIVATE
        wiirecomp
        # We will also link Raylib, ImGui, and other dependencies here
)

message(STATUS "nWiiRecomp project configured. Ready to build.")
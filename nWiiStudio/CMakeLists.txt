cmake_minimum_required(VERSION 3.21)
project(nWiiStudio)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# Optional: raygui for UI elements
FetchContent_Declare(
    raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib raygui)

file(GLOB_RECURSE STUDIO_SOURCES "src/*.cpp")
file(GLOB_RECURSE STUDIO_HEADERS "include/*.hpp")

add_executable(nWiiStudio ${STUDIO_SOURCES} ${STUDIO_HEADERS})

target_include_directories(nWiiStudio PRIVATE 
    include
    ${raygui_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiAnalyzer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiRecomp/include
)

target_link_libraries(nWiiStudio PRIVATE
    raylib
)

# Optional: link platform specific libraries if needed
if (WIN32)
    target_link_libraries(nWiiStudio PRIVATE winmm)
endif()

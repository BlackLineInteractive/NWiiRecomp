cmake_minimum_required(VERSION 3.20)
project(nWiiRecomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(nWiiLoader)
add_subdirectory(nWiiAnalyzer)
add_subdirectory(nWiiRecomp)
add_subdirectory(nWiiRuntime)

# If nWiiStudio relies on external libs like raylib that aren't set up yet, 
# it might fail. Let's include it cautiously or just add it.
add_subdirectory(nWiiStudio)
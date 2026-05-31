cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SRC_FILES "src/*.cpp")
list(REMOVE_ITEM SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(nwiirecomp_lib STATIC ${SRC_FILES})
target_include_directories(nwiirecomp_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiAnalyzer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiRuntime/include
)
target_link_libraries(nwiirecomp_lib PUBLIC nwiiruntime nwiianalyzer)

add_executable(nwiirecomp "src/main.cpp")
target_link_libraries(nwiirecomp PRIVATE nwiirecomp_lib)


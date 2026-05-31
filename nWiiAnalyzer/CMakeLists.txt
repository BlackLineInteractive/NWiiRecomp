cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_library(nwiianalyzer STATIC ${SRC_FILES})

target_include_directories(nwiianalyzer PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiRuntime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiRecomp/include
)

target_link_libraries(nwiianalyzer PRIVATE nwiiruntime)

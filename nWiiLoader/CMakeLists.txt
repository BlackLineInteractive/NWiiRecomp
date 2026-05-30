cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_library(nwiiloader STATIC ${SRC_FILES})

target_include_directories(nwiiloader PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiRecomp/include
)

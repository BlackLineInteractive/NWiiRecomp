cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_executable(nwiirecomp ${SRC_FILES})

target_include_directories(nwiirecomp PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../nWiiLoader/include
)

target_link_libraries(nwiirecomp PRIVATE nwiiloader)


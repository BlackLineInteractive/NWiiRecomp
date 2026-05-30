cmake_minimum_required(VERSION 3.20)

add_library(nwiiloader STATIC 
    loader.cpp
)

target_include_directories(nwiiloader PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

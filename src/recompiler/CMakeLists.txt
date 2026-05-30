cmake_minimum_required(VERSION 3.20)

add_executable(nwiirecomp 
    main.cpp
)

target_include_directories(nwiirecomp PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

cmake_minimum_required(VERSION 3.20)

add_library(nwiiruntime STATIC
    main.cpp
)

target_include_directories(nwiiruntime PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

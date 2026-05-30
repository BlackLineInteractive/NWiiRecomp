cmake_minimum_required(VERSION 3.20)

# Analyzer / GUI Tool
add_executable(nwiianalyzer 
    main.cpp
)

target_include_directories(nwiianalyzer PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Link against the loader and other core libraries in the future
target_link_libraries(nwiianalyzer PRIVATE nwiiloader)
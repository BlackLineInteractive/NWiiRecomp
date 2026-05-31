cmake_minimum_required(VERSION 3.20)

add_library(nwiiruntime STATIC
    src/main.cpp
    src/loader.cpp
    src/hle/os.cpp
    src/hle/memory.cpp
    src/hle/gx.cpp
    src/hle/pad.cpp
    src/hle/dvd.cpp
    src/hle/ax.cpp
)

target_link_libraries(nwiiruntime PUBLIC raylib)

target_include_directories(nwiiruntime PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

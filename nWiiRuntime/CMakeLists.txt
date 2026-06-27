cmake_minimum_required(VERSION 3.20)

include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)
add_library(nwiiruntime STATIC
    src/loader.cpp
    src/config.cpp
    src/input/input_manager.cpp
    src/hle/os.cpp
    src/hle/memory.cpp
    src/hle/gx.cpp
    src/hle/pad.cpp
    src/hle/dvd.cpp
    src/hle/ax.cpp
    src/hle/ios.cpp
    src/hle/ios_kernel.cpp
    src/hle/devices/di_device.cpp
    src/hle/devices/fs_device.cpp
    src/hle/devices/stm_device.cpp
    src/hle/devices/usb_device.cpp
    src/hle/devices/es_device.cpp
    src/hle/cafe/cafe_os.cpp
    src/hle/cafe/latte_gpu.cpp
)

target_link_libraries(nwiiruntime PUBLIC raylib tomlplusplus::tomlplusplus)

target_include_directories(nwiiruntime PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# add_executable(nwiiruntime_exe src/main.cpp)
# target_link_libraries(nwiiruntime_exe PRIVATE nwiiruntime)

# target_link_libraries(nwiiruntime_exe PRIVATE nwiirecomp_lib)

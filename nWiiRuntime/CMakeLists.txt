cmake_minimum_required(VERSION 3.20)

include(FetchContent)
add_library(nwiiruntime STATIC
    src/core/loader.cpp
    src/core/config.cpp
    src/core/os_hle_stubs.cpp
    src/core/event_scheduler.cpp
    src/input/input_manager.cpp
    src/input/sources/mouse_keyboard_source.cpp
    src/input/sources/gamepad_source.cpp
    src/input/sources/phone_source.cpp
    src/hle/memory.cpp
    src/hle/gx.cpp
    src/hle/pad.cpp
    src/hle/dvd.cpp
    src/hle/gx/fifo_parser.cpp
    src/hle/gx/renderer_gl.cpp
    src/hle/gx/renderer_factory.cpp
    src/hle/gx/tev_shader_gen.cpp
    src/hle/texture_cache_gl.cpp
    src/hle/ax.cpp
    src/hle/ios.cpp
    src/hle/ios_kernel.cpp
    src/hle/virtual_disc.cpp
    src/hw/mmio_dispatcher.cpp
    src/platform/platform.cpp
    src/platform/wii_platform.cpp
    src/platform/gc_platform.cpp
    src/hw/hw_pi.cpp
    src/hw/hw_pe.cpp
    src/hw/hw_vi.cpp
    src/hw/hw_dsp.cpp
    src/hw/hw_exi.cpp
    src/hw/hw_si.cpp
    src/hw/hw_ai.cpp
    src/hw/hw_di.cpp
    src/hw/hw_ipc.cpp
    src/hw/hw_all.cpp
    src/hw/hw_mi.cpp
    src/hw/hw_cp.cpp
    src/hle/devices/di_device.cpp
    src/hle/devices/fs_device.cpp
    src/hle/devices/stm_device.cpp
    src/hle/devices/usb_device.cpp
    src/hle/devices/es_device.cpp
    src/hle/interpreter.cpp
    src/platform/glad/src/glad.c
)

target_link_libraries(nwiiruntime PUBLIC SDL2-static tomlplusplus::tomlplusplus)

if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(METAL_LIBRARY Metal)
    find_library(QUARTZCORE_LIBRARY QuartzCore)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREVIDEO_LIBRARY CoreVideo)
    target_link_libraries(nwiiruntime PUBLIC 
        ${COCOA_LIBRARY} 
        ${METAL_LIBRARY} 
        ${QUARTZCORE_LIBRARY}
        ${IOKIT_LIBRARY} 
        ${COREVIDEO_LIBRARY}
    )
    target_sources(nwiiruntime PRIVATE src/hle/gx/renderer_metal.mm)
endif()

target_include_directories(nwiiruntime PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/glad/include
)

# add_executable(nwiiruntime_exe src/main.cpp)
# target_link_libraries(nwiiruntime_exe PRIVATE nwiiruntime)

# target_link_libraries(nwiiruntime_exe PRIVATE nwiirecomp_lib)

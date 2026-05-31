#include "runtime/cpu_context.h"
#include <iostream>

using namespace nwii::runtime;

extern "C" {

// Initialize DVD subsystem
void DVDInit(CPUContext& ctx) {
    std::cout << "[HLE DVD] DVDInit: Initializing DVD subsystem" << std::endl;
}

// Open a file from the DVD
// r3 = filename (char*), r4 = DVDFileInfo*
// Returns 1 (true) on success, 0 on failure
void DVDOpen(CPUContext& ctx) {
    uint32_t filename_addr = ctx.gpr[3];
    uint32_t file_info_addr = ctx.gpr[4];
    
    std::cout << "[HLE DVD] DVDOpen: filename_addr=" << std::hex << filename_addr 
              << ", info_addr=" << file_info_addr << std::dec << std::endl;
              
    // Stub: always return success (1)
    ctx.gpr[3] = 1;
}

// Read from an open DVD file asynchronously
// r3 = DVDFileInfo*, r4 = buffer, r5 = length, r6 = offset, r7 = callback
// Returns 1 (true) on success
void DVDReadAsyncPrio(CPUContext& ctx) {
    uint32_t file_info_addr = ctx.gpr[3];
    uint32_t buffer_addr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];
    uint32_t offset = ctx.gpr[6];
    uint32_t callback_addr = ctx.gpr[7];
    
    std::cout << "[HLE DVD] DVDReadAsyncPrio: info=" << std::hex << file_info_addr 
              << ", buf=" << buffer_addr << ", len=" << std::dec << length 
              << ", offset=" << offset << ", cb=" << std::hex << callback_addr << std::dec << std::endl;
              
    // Stub: just return success, real impl would need to trigger callback later
    ctx.gpr[3] = 1;
}

// Close an open DVD file
// r3 = DVDFileInfo*
// Returns 1 (true) on success
void DVDClose(CPUContext& ctx) {
    uint32_t file_info_addr = ctx.gpr[3];
    std::cout << "[HLE DVD] DVDClose: info=" << std::hex << file_info_addr << std::dec << std::endl;
    ctx.gpr[3] = 1;
}

// Get the current DVD drive status
// Returns status code (e.g. 0 for ready)
void DVDGetDriveStatus(CPUContext& ctx) {
    // Return ready
    ctx.gpr[3] = 0;
}

// Read from DVD synchronously
// r3 = DVDFileInfo*, r4 = buffer, r5 = length, r6 = offset
// Returns bytes read or status code
void DVDReadPrio(CPUContext& ctx) {
    uint32_t file_info_addr = ctx.gpr[3];
    uint32_t buffer_addr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];
    uint32_t offset = ctx.gpr[6];
    
    std::cout << "[HLE DVD] DVDReadPrio: info=" << std::hex << file_info_addr 
              << ", buf=" << buffer_addr << ", len=" << std::dec << length 
              << ", offset=" << offset << std::endl;
              
    // Stub: pretend we read the requested length
    ctx.gpr[3] = length;
}

} // extern "C"

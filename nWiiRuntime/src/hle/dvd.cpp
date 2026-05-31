#include "runtime/cpu_context.h"
#include "runtime/config.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <memory>

using namespace nwii::runtime;

// Map GC virtual addresses (DVDFileInfo pointers) to host file streams
static std::unordered_map<uint32_t, std::shared_ptr<std::ifstream>> g_open_dvd_files;

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
    
    // Read the null-terminated filename string from simulated memory
    std::string filename;
    uint32_t addr = filename_addr;
    while (true) {
        char c = (char)ctx.mmu.read8(addr++);
        if (c == '\0') break;
        filename += c;
    }
    
    // Construct the full host path
    std::filesystem::path game_dir = Config::get().game_dir;
    std::filesystem::path full_path = game_dir / filename;
    
    std::cout << "[HLE DVD] DVDOpen: '" << filename << "' -> " << full_path << " (info_addr=" << std::hex << file_info_addr << std::dec << ")\n";
    
    auto stream = std::make_shared<std::ifstream>(full_path, std::ios::binary);
    if (!stream->is_open()) {
        std::cerr << "[HLE DVD] ERROR: Failed to open file: " << full_path << "\n";
        ctx.gpr[3] = 0; // Failure
        return;
    }
    
    // Store in our open files map
    g_open_dvd_files[file_info_addr] = stream;
    
    // Write out the file size to the DVDFileInfo structure (offset 0x3C is standard for length)
    stream->seekg(0, std::ios::end);
    uint32_t length = stream->tellg();
    stream->seekg(0, std::ios::beg);
    ctx.mmu.write32(file_info_addr + 0x3C, length);
    
    ctx.gpr[3] = 1; // Success
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
              
    auto it = g_open_dvd_files.find(file_info_addr);
    if (it != g_open_dvd_files.end() && it->second->is_open()) {
        auto& stream = it->second;
        stream->seekg(offset);
        
        // Read directly into MMU memory using a buffer
        std::vector<uint8_t> temp_buf(length);
        stream->read(reinterpret_cast<char*>(temp_buf.data()), length);
        uint32_t bytes_read = stream->gcount();
        
        for (uint32_t i = 0; i < bytes_read; i++) {
            ctx.mmu.write8(buffer_addr + i, temp_buf[i]);
        }
    } else {
        std::cerr << "[HLE DVD] ERROR: DVDReadAsyncPrio called on invalid/closed file info!\n";
    }
    
    // TODO: Fire callback if callback_addr != 0
    if (callback_addr != 0) {
        std::cerr << "[HLE DVD] WARNING: Ignoring callback 0x" << std::hex << callback_addr << std::dec << " (Not yet implemented)\n";
    }
    
    ctx.gpr[3] = 1; // Success
}

// Close an open DVD file
// r3 = DVDFileInfo*
// Returns 1 (true) on success
void DVDClose(CPUContext& ctx) {
    uint32_t file_info_addr = ctx.gpr[3];
    std::cout << "[HLE DVD] DVDClose: info=" << std::hex << file_info_addr << std::dec << std::endl;
    
    auto it = g_open_dvd_files.find(file_info_addr);
    if (it != g_open_dvd_files.end()) {
        g_open_dvd_files.erase(it); // Closes the stream via shared_ptr destructor
    }
    
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
              
    auto it = g_open_dvd_files.find(file_info_addr);
    if (it != g_open_dvd_files.end() && it->second->is_open()) {
        auto& stream = it->second;
        stream->seekg(offset);
        
        std::vector<uint8_t> temp_buf(length);
        stream->read(reinterpret_cast<char*>(temp_buf.data()), length);
        uint32_t bytes_read = stream->gcount();
        
        for (uint32_t i = 0; i < bytes_read; i++) {
            ctx.mmu.write8(buffer_addr + i, temp_buf[i]);
        }
        
        ctx.gpr[3] = bytes_read;
    } else {
        ctx.gpr[3] = -1; // Error
    }
}

} // extern "C"

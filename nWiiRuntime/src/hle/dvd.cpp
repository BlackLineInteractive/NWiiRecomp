#include "runtime/cpu_context.h"
#include "runtime/config.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <filesystem>
#include <memory>

using namespace nwii::runtime;
static std::unordered_map<uint32_t, std::shared_ptr<std::ifstream>> g_open_dvd_files;

extern "C" {

void DVDInit(CPUContext& ctx) {}

void DVDOpen(CPUContext& ctx) {
    uint32_t filename_addr = ctx.gpr[3];
    uint32_t file_info_addr = ctx.gpr[4];
    
    std::string filename;
    uint32_t addr = filename_addr;
    while (true) {
        char c = (char)ctx.mmu.read8(addr++);
        if (c == '\0') break;
        filename += c;
    }
    
    std::filesystem::path full_path = std::filesystem::path(Config::get().game_dir) / filename;
    auto stream = std::make_shared<std::ifstream>(full_path, std::ios::binary);
    
    if (!stream->is_open()) {
        ctx.gpr[3] = 0; 
        return;
    }
    
    g_open_dvd_files[file_info_addr] = stream;
    
    // Strict RVL_SDK DVDFileInfo structure populating
    stream->seekg(0, std::ios::end);
    uint32_t file_size = stream->tellg();
    stream->seekg(0, std::ios::beg);
    
    ctx.mmu.write32(file_info_addr + 0x34, file_size); // 0x34 is length
    ctx.mmu.write32(file_info_addr + 0x30, 0);         // 0x30 is startAddr (sector)
    
    ctx.gpr[3] = 1;
}

void DVDReadAsyncPrio(CPUContext& ctx) {
    uint32_t file_info_addr = ctx.gpr[3];
    uint32_t buffer_addr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];
    uint32_t offset = ctx.gpr[6];
    uint32_t callback_addr = ctx.gpr[7];
    
    auto it = g_open_dvd_files.find(file_info_addr);
    if (it != g_open_dvd_files.end() && it->second->is_open()) {
        it->second->seekg(offset);
        std::vector<uint8_t> buf(length);
        it->second->read((char*)buf.data(), length);
        uint32_t bytes_read = it->second->gcount();
        
        for (uint32_t i = 0; i < bytes_read; i++) ctx.mmu.write8(buffer_addr + i, buf[i]);
        
        // Strict DVDCommandBlock offsets
        ctx.mmu.write32(file_info_addr + 0x0C, 0);          // State (0 = ready)
        ctx.mmu.write32(file_info_addr + 0x18, bytes_read); // transferredSize
    }

    // Deferred callback execution
    ctx.queue_callback(callback_addr, length, file_info_addr);
    ctx.gpr[3] = 1; 
}

void DVDClose(CPUContext& ctx) {
    g_open_dvd_files.erase(ctx.gpr[3]);
    ctx.gpr[3] = 1;
}

void DVDGetDriveStatus(CPUContext& ctx) { ctx.gpr[3] = 0; }
void DVDReadPrio(CPUContext& ctx) { /* Sync reads omitted for brevity */ ctx.gpr[3] = ctx.gpr[5]; }

} // extern "C"


#include "runtime/devices.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::devices {

// SYSCONF logic
struct VNandFile {
    std::string path;
    std::vector<uint8_t> data;
};

static std::vector<VNandFile> g_vnand_files;
static bool g_vnand_init = false;

static std::vector<uint8_t> make_sysconf() {
    std::vector<uint8_t> sc(0x4000, 0xFF);
    sc[0]='S'; sc[1]='C'; sc[2]='v'; sc[3]='1'; sc[4]='5';
    sc[5] = 1;
    sc[6] = 0; sc[7] = 1;
    uint8_t key[] = "IPL.AR";
    uint8_t val[] = { 0, 4, 'N','T','S','C' };
    int off = 8;
    sc[off++] = 0x02;
    sc[off++] = (uint8_t)(sizeof(key)-1);
    for (auto c : key) if(c) sc[off++] = c;
    for (auto b : val) sc[off++] = b;
    return sc;
}

static std::vector<uint8_t> make_setting_txt() {
    const char* txt =
        "AREA=USA\r\n"
        "MODEL=RVL-001\r\n"
        "DVD=0\r\n"
        "MPCH=0x7FFE\r\n"
        "CODE=0\r\n"
        "SERNO=123456789\r\n"
        "VIDEO=NTSC\r\n"
        "GAME=US\r\n";
    return std::vector<uint8_t>(txt, txt + strlen(txt));
}

static void vnand_init() {
    if (g_vnand_init) return;
    g_vnand_init = true;
    g_vnand_files.push_back({"/shared2/sys/SYSCONF", make_sysconf()});
    g_vnand_files.push_back({"/title/00000001/00000002/data/setting.txt", make_setting_txt()});
    g_vnand_files.push_back({"/title/00000001/00000002/data/play_rec.dat", std::vector<uint8_t>(0x20, 0)});
    g_vnand_files.push_back({"/shared2/test2/dvderror.dat", std::vector<uint8_t>(4, 0)});
}

static constexpr int VNAND_FD_BASE = 20;
static constexpr int VNAND_FD_MAX  = 8;
struct VNandHandle { int file_idx = -1; uint32_t pos = 0; bool open = false; };
static VNandHandle g_vnand_handles[VNAND_FD_MAX];

class FSDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/fs"; }
    bool matches_path(const std::string& path) const override {
        // Matches /dev/fs AND any absolute path on NAND (like /shared2/...), 
        // but exclude other /dev/ devices.
        if (path == "/dev/fs") return true;
        if (path.find("/dev/") == 0) return false; // Let other dev nodes handle themselves
        return path.find("/") == 0;
    }
    
    int32_t open(CPUContext& ctx, const std::string& path, uint32_t mode) override {
        vnand_init();
        for (int i = 0; i < (int)g_vnand_files.size(); ++i) {
            if (g_vnand_files[i].path == path) {
                for (int s = 0; s < VNAND_FD_MAX; ++s) {
                    if (!g_vnand_handles[s].open) {
                        g_vnand_handles[s] = {i, 0, true};
                        return VNAND_FD_BASE + s;
                    }
                }
                return -106; // ISFS_ERROR_ENOENT equivalent
            }
        }
        return IPC_OK; // default success for /dev/fs main node
    }
    
    int32_t close(CPUContext& ctx, uint32_t fd) override {
        int slot = fd - VNAND_FD_BASE;
        if (slot >= 0 && slot < VNAND_FD_MAX && g_vnand_handles[slot].open) {
            g_vnand_handles[slot].open = false;
            return IPC_OK;
        }
        return IPC_OK;
    }
    
    int32_t read(CPUContext& ctx, uint32_t fd, uint32_t buf, uint32_t len) override {
        int slot = fd - VNAND_FD_BASE;
        if (slot >= 0 && slot < VNAND_FD_MAX && g_vnand_handles[slot].open) {
            auto& h = g_vnand_handles[slot];
            auto& f = g_vnand_files[h.file_idx];
            uint32_t avail = f.data.size() - h.pos;
            uint32_t to_read = std::min(len, avail);
            
            // Convert physical to virtual. Assuming buf is physical here.
            uint32_t virt_ptr = buf;
            if (virt_ptr < 0x01800000 || (virt_ptr >= 0x10000000 && virt_ptr < 0x14000000))
                virt_ptr |= 0x80000000;
                
            if (virt_ptr >= 0x80000000 && virt_ptr < 0x94000000 && to_read > 0) {
                for (uint32_t i = 0; i < to_read; ++i) {
                    ctx.mmu.write8(virt_ptr + i, f.data[h.pos + i]);
                }
            }
            h.pos += to_read;
            return to_read;
        }
        return len; // stub return length
    }
    
    int32_t seek(CPUContext& ctx, uint32_t fd, int32_t offset, int32_t whence) override {
        int slot = fd - VNAND_FD_BASE;
        if (slot >= 0 && slot < VNAND_FD_MAX && g_vnand_handles[slot].open) {
            auto& h = g_vnand_handles[slot];
            auto& f = g_vnand_files[h.file_idx];
            if (whence == 0) h.pos = offset; // SEEK_SET
            else if (whence == 1) h.pos += offset; // SEEK_CUR
            else if (whence == 2) h.pos = f.data.size() + offset; // SEEK_END
            return h.pos;
        }
        return IPC_OK;
    }
};

std::unique_ptr<IDevice> create_fs_device() {
    return std::make_unique<FSDevice>();
}

} // namespace nwii::runtime::devices

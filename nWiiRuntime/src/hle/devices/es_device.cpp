#include "runtime/devices.h"
#include "runtime/ios_kernel.h"
#include "runtime/config.h"
#include <iostream>

namespace nwii::runtime::devices {

class ESDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/es"; }
    bool matches_path(const std::string& path) const override {
        return path == get_name();
    }
    
    int32_t ioctl(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[ES] ioctl cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }
    int32_t ioctlv(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[ES] ioctlv cmd=0x" << std::hex << req.ioctl_cmd << std::dec 
                  << " in=" << req.arg_cnt_in << " out=" << req.arg_cnt_out << std::endl;
        
        // Zero out all output vectors by default
        for (uint32_t i = req.arg_cnt_in; i < req.arg_cnt_in + req.arg_cnt_out; i++) {
            uint32_t addr = req.ioctlv_vecs[i].addr;
            uint32_t len = req.ioctlv_vecs[i].len;
            if (addr != 0 && len > 0) {
                if (len > 0x10000) {
                    std::cout << "[ES] Warning: Huge ioctlv output buffer! len=0x" << std::hex << len << std::dec << "\n";
                    len = 0x10000; // Cap to prevent hang
                }
                for (uint32_t j = 0; j < len; j++) {
                    ctx.mmu.write8(addr + j, 0);
                }
            }
        }
        std::cout << "[ES] Output vectors zeroed.\n";

        // Handle specific commands
        // Handle specific commands
        if (req.ioctl_cmd == 0x20) { // ES_GetTitleId
            if (req.arg_cnt_out >= 1) {
                uint32_t view_addr = req.ioctlv_vecs[req.arg_cnt_in].addr;
                uint32_t view_len = req.ioctlv_vecs[req.arg_cnt_in].len;
                if (view_addr != 0 && view_len >= 8) {
                    const std::string& gid = nwii::runtime::Config::get().game_id;
                    uint32_t title_id_high = 0x00010000;
                    uint32_t title_id_low = 0;
                    for (size_t i = 0; i < 4 && i < gid.size(); i++) {
                        title_id_low |= ((uint32_t)gid[i]) << ((3 - i) * 8);
                    }
                    ctx.mmu.write32(view_addr + 0, title_id_high);
                    ctx.mmu.write32(view_addr + 4, title_id_low); 
                    std::cout << "[ES] Returned Title ID 00010000-" << std::hex << title_id_low << std::dec << " (" << gid << ")\n";
                } else {
                    std::cout << "[ES] Warning: ES_GetTitleId output buffer too small (len=" << view_len << ")\n";
                }
            }
        } else if (req.ioctl_cmd == 0x1E) { // ES_GetDeviceCert
            if (req.arg_cnt_out >= 1) {
                uint32_t view_addr = req.ioctlv_vecs[req.arg_cnt_in].addr;
                uint32_t view_len = req.ioctlv_vecs[req.arg_cnt_in].len;
                if (view_addr != 0 && view_len >= 0x180) {
                    static constexpr uint8_t DEVICE_CERT[0x180] = {
                        0x00, 0x01, 0x00, 0x02, 0x00, 0x54, 0xe3, 0x9a, 0x0f, 0xe6, 0xe1, 0x61, 0xb6, 0x2f, 0x9d,
                        0x0c, 0xaa, 0x1e, 0xc5, 0x58, 0x85, 0xa1, 0xeb, 0x93, 0xa5, 0x1e, 0xf4, 0x06, 0x99, 0x77,
                        0x9a, 0x46, 0x76, 0x01, 0x00, 0xb7, 0xe4, 0x72, 0x10, 0x6e, 0xa2, 0x21, 0x57, 0xe0, 0xe3,
                        0xbe, 0x48, 0x9d, 0x7b, 0xa5, 0x2d, 0x46, 0x2f, 0x33, 0x93, 0xae, 0xb0, 0x4b, 0x53, 0xcb,
                        0xb9, 0xef, 0x16, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x6f, 0x6f, 0x74, 0x2d, 0x43, 0x41,
                        0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31, 0x2d, 0x4d, 0x53, 0x30, 0x30, 0x30, 0x30,
                        0x30, 0x30, 0x30, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x02, 0x4e, 0x47, 0x30, 0x34, 0x65, 0x35, 0x34, 0x32, 0x31, 0x64, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x6f, 0x1e, 0x5f, 0x58, 0x01, 0xa8, 0x1a, 0x89, 0x8d, 0x04,
                        0xe4, 0x0e, 0x44, 0x6c, 0x99, 0x52, 0xef, 0xe8, 0xe9, 0x8a, 0xec, 0x2b, 0x73, 0xea, 0x13,
                        0x56, 0x93, 0xf5, 0x1a, 0xd8, 0x53, 0xa8, 0xc5, 0xf2, 0x00, 0x41, 0xe9, 0x5e, 0x0a, 0x5d,
                        0x0c, 0xdf, 0xf0, 0xc6, 0x96, 0x2c, 0x98, 0x96, 0xa9, 0x0f, 0xf0, 0x2e, 0x1f, 0x0d, 0x1a,
                        0xcf, 0xa8, 0x35, 0x52, 0x74, 0x36, 0x13, 0x88, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                    };
                    for (uint32_t j = 0; j < 0x180; j++) {
                        ctx.mmu.write8(view_addr + j, DEVICE_CERT[j]);
                    }
                }
            }
        } else if (req.ioctl_cmd == 0x1B) { // ES_DiGetTicketView
            if (req.arg_cnt_out >= 1) {
                uint32_t view_addr = req.ioctlv_vecs[req.arg_cnt_in].addr;
                if (view_addr) {
                    const std::string& gid = nwii::runtime::Config::get().game_id;
                    uint32_t title_id_high = 0x00010000;
                    uint32_t title_id_low = 0;
                    for (size_t i = 0; i < 4 && i < gid.size(); i++) {
                        title_id_low |= ((uint32_t)gid[i]) << ((3 - i) * 8);
                    }
                    ctx.mmu.write32(view_addr + 0x0, 0); // view_version
                    ctx.mmu.write32(view_addr + 0x4, 0); // ticket_id high
                    ctx.mmu.write32(view_addr + 0x8, 0); // ticket_id low
                    ctx.mmu.write32(view_addr + 0xC, 0); // dev_type
                    ctx.mmu.write32(view_addr + 0x10, title_id_high);
                    ctx.mmu.write32(view_addr + 0x14, title_id_low);
                    ctx.mmu.write16(view_addr + 0x18, 0xFFFF); // access_mask
                    
                    std::cout << "[ES] Returning TicketView for TitleID " << std::hex << title_id_high << "-" << title_id_low << std::dec << std::endl;
                }
            }
            return IPC_OK;
        } else if (req.ioctl_cmd == 0x24) { // ES_GetTicketViews
            // Output 0: Array of TicketViews
            // Output 1: viewCount
            if (req.arg_cnt_out >= 2) {
                uint32_t count_addr = req.ioctlv_vecs[req.arg_cnt_in + 1].addr;
                if (count_addr) {
                    ctx.mmu.write32(count_addr, 1); // We have 1 ticket
                }
                
                uint32_t view_addr = req.ioctlv_vecs[req.arg_cnt_in].addr;
                if (view_addr && req.arg_cnt_in > 0) {
                    // Try to copy the requested Title ID into the ticket view
                    // Input 0: TitleID (8 bytes)
                    uint32_t tid_addr = req.ioctlv_vecs[0].addr;
                    if (tid_addr) {
                        uint64_t tid = ((uint64_t)ctx.mmu.read32(tid_addr) << 32) | ctx.mmu.read32(tid_addr + 4);
                        // Write TitleID into TicketView (usually at offset 0x1C8 or similar, but for view it's offset 0x0)
                        // Actually, just putting the TitleID at offset 0 of the view might be enough for a naive check.
                        ctx.mmu.write32(view_addr + 0, (uint32_t)(tid >> 32));
                        ctx.mmu.write32(view_addr + 4, (uint32_t)(tid & 0xFFFFFFFF));
                    }
                }
            }
        } else if (req.ioctl_cmd == 0x26) { // ES_GetTMDView
            // Input 0: TitleID
            // Output 0: Array of TMDViews
            // Output 1: viewCount
            if (req.arg_cnt_out >= 2) {
                uint32_t count_addr = req.ioctlv_vecs[req.arg_cnt_in + 1].addr;
                if (count_addr) ctx.mmu.write32(count_addr, 1); // 1 TMD
            }
        }

        return IPC_OK;
    }
};

std::unique_ptr<IDevice> create_es_device() {
    return std::make_unique<ESDevice>();
}

void register_all() {
    auto& kernel = IOSKernel::get();
    kernel.register_device(create_di_device());
    kernel.register_device(create_fs_device());
    kernel.register_device(create_stm_device());
    kernel.register_device(create_usb_device());
    kernel.register_device(create_es_device());
}

} // namespace nwii::runtime::devices

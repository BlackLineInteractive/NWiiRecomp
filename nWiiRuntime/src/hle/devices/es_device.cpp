#include "runtime/devices.h"
#include "runtime/ios_kernel.h"
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
                for (uint32_t j = 0; j < len; j++) {
                    ctx.mmu.write8(addr + j, 0);
                }
            }
        }

        // Handle specific commands
        if (req.ioctl_cmd == 0x24) { // ES_GetTicketViews
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

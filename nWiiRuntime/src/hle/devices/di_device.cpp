#include "runtime/devices.h"
#include "runtime/config.h"
#include "runtime/virtual_disc.h"
#include <iostream>
#include <vector>

extern void di_read_internal_shared(nwii::runtime::MMU* mmu, uint32_t inbuf, uint32_t outbuf,
                                     uint32_t callback, uint32_t userdata, bool is_async,
                                     uint32_t out_size);

namespace nwii::runtime::devices {

static uint32_t di_phys_to_virt(uint32_t addr) {
    if (addr != 0 && addr < 0x80000000) {
        if (addr < 0x01800000)
            return addr | 0x80000000;
        if (addr >= 0x10000000 && addr < 0x14000000)
            return addr | 0x80000000;
    }
    return addr;
}

class DIDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/di"; }

    int32_t ioctl(CPUContext& ctx, const IpcRequest& req) override {
        const std::string& gid = Config::get().game_id;
        uint32_t cmd = req.ioctl_cmd;
        
        if (cmd == 0x01) cmd = 0x12;

        if (cmd == 0x12) { 
            uint32_t outbuf = req.out_buf;
            if (outbuf != 0 && outbuf < 0x80000000) {
                if (outbuf < 0x01800000 || (outbuf >= 0x10000000 && outbuf < 0x14000000))
                    outbuf |= 0x80000000;
            }
            if (outbuf >= 0x80000000 && outbuf < 0x94000000) {
                for (int i = 0; i < 32; i++) ctx.mmu.write8(outbuf + i, 0);
                ctx.mmu.write8(outbuf + 0, 0x01); 
                ctx.mmu.write8(outbuf + 1, 0x00);
                ctx.mmu.write8(outbuf + 8,  'R'); ctx.mmu.write8(outbuf + 9,  'V');
                ctx.mmu.write8(outbuf + 10, 'L'); ctx.mmu.write8(outbuf + 11, '-');
                ctx.mmu.write8(outbuf + 12, 'D'); ctx.mmu.write8(outbuf + 13, 'I');
                std::cout << "[DI] DVDLowInquiry: wrote RVL-DI to 0x" << std::hex << outbuf << std::dec << std::endl;
            }
            return 1; 
        } else if (cmd == 0x8E) { 
            uint32_t buf = req.out_buf;
            if (buf != 0 && buf < 0x80000000) {
                if (buf < 0x01800000 || (buf >= 0x10000000 && buf < 0x14000000))
                    buf |= 0x80000000;
            }
            if (buf >= 0x80000000 && buf < 0x94000000) {
                for (int i = 0; i < 32; i++) ctx.mmu.write8(buf + i, 0);
                for (int i = 0; i < (int)gid.size() && i < 4; i++)
                    ctx.mmu.write8(buf + i, (uint8_t)gid[i]);
                ctx.mmu.write8(buf + 4, '0');
                ctx.mmu.write8(buf + 5, '1');
                ctx.mmu.write8(buf + 6, '0');
                ctx.mmu.write8(buf + 7, '1');
                ctx.mmu.write32(buf + 0x18, 0x5D1C9EA3); 
                std::cout << "[DI] ReadDiskID: wrote " << gid << " to 0x" << std::hex << buf << std::dec << std::endl;
            }
            return 1;
        } else if (cmd == 0x80) { 
            std::cout << "[DI] Reset acknowledged" << std::endl;
            return 1;
        } else if (cmd == 0x71 || cmd == 0x8D) {
            // DVDLowRead / DVDLowUnencryptedRead (Dolphin DI layout):
            
            uint32_t in = di_phys_to_virt(req.in_buf);
            uint32_t length = ctx.mmu.read32(in + 4);
            uint64_t offset = (uint64_t)ctx.mmu.read32(in + 8) << 2;
            uint32_t dst = di_phys_to_virt(req.out_buf);

            auto& vd = VirtualDisc::get();
            if (!vd.valid())
                vd.init(Config::get().game_dir);

            std::cout << "[DI] " << (cmd == 0x71 ? "Read" : "UnencryptedRead")
                      << ": offset=0x" << std::hex << offset << " len=0x" << length
                      << " dst=0x" << dst << std::dec << std::endl;

            if (dst >= 0x80000000 && dst < 0x94000000 && length > 0 && length < 0x4000000) {
                std::vector<uint8_t> tmp(length);
                if (vd.valid() && vd.read(offset, length, tmp.data())) {
                    for (uint32_t i = 0; i < length; ++i)
                        ctx.mmu.write8(dst + i, tmp[i]);
                } else {
                    
                    di_read_internal_shared(&ctx.mmu, req.in_buf, req.out_buf, 0, 0, false,
                                            req.out_size);
                }
            }
            return 1; 
        } else if (cmd == 0x70) { 
            uint32_t dst = di_phys_to_virt(req.out_buf);
            auto& vd = VirtualDisc::get();
            if (!vd.valid())
                vd.init(Config::get().game_dir);
            if (dst >= 0x80000000 && dst < 0x94000000) {
                if (vd.valid()) {
                    const auto& boot = vd.boot_data();
                    for (uint32_t i = 0; i < 0x20 && i < boot.size(); ++i)
                        ctx.mmu.write8(dst + i, boot[i]);
                } else {
                    for (int i = 0; i < (int)gid.size() && i < 4; i++)
                        ctx.mmu.write8(dst + i, (uint8_t)gid[i]);
                    ctx.mmu.write32(dst + 0x18, 0x5D1C9EA3);
                }
            }
            return 1;
        } else if (cmd == 0x8B) { 
            std::cout << "[DI] OpenPartition acknowledged" << std::endl;
            return 1;
        } else if (cmd == 0x8C) { 
            return 1;
        } else if (cmd == 0x20 || cmd == 0x95 || cmd == 0x96 || cmd == 0xe3) { 
            
            di_read_internal_shared(&ctx.mmu, req.in_buf, req.out_buf, 0, 0, false,
                                    req.out_size);
            return 1; 
        } else if (cmd == 0x86) { 
            return 1;
        } else if (cmd == 0xE0) { 
            return 1;
        } else if (cmd == 0x60) { 
            return 1;
        }

        std::cout << "[DI] Unhandled ioctl: 0x" << std::hex << cmd << std::dec << std::endl;
        return 1;
    }

    int32_t ioctlv(CPUContext& ctx, const IpcRequest& req) override {
        if (req.ioctl_cmd == 0x8B) { 
            
            for (uint32_t i = req.arg_cnt_in; i < req.arg_cnt_in + req.arg_cnt_out
                 && i < req.ioctlv_vecs.size(); ++i) {
                uint32_t addr = di_phys_to_virt(req.ioctlv_vecs[i].addr);
                uint32_t len = req.ioctlv_vecs[i].len;
                if (addr >= 0x80000000 && addr < 0x94000000 && len <= 0x2000)
                    for (uint32_t j = 0; j < len; ++j)
                        ctx.mmu.write8(addr + j, 0);
            }
            std::cout << "[DI] OpenPartition (ioctlv) acknowledged" << std::endl;
            return 1;
        }
        std::cout << "[DI] Unhandled ioctlv: 0x" << std::hex << req.ioctl_cmd
                  << std::dec << std::endl;
        return 1;
    }
};

std::unique_ptr<IDevice> create_di_device() {
    return std::make_unique<DIDevice>();
}

} 

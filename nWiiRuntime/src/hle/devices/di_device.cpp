#include "runtime/devices.h"
#include "runtime/config.h"
#include <iostream>

// Forward declaration of di_read_internal_shared from ios.cpp (temporary until fully extracted)
extern void di_read_internal_shared(nwii::runtime::MMU* mmu, uint32_t inbuf,
                                     uint32_t callback, uint32_t userdata, bool is_async);

namespace nwii::runtime::devices {

class DIDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/di"; }

    int32_t ioctl(CPUContext& ctx, const IpcRequest& req) override {
        const std::string& gid = Config::get().game_id;
        uint32_t cmd = req.ioctl_cmd;
        
        if (cmd == 0x01) cmd = 0x12;

        if (cmd == 0x12) { // DVDLowInquiry
            uint32_t outbuf = req.out_buf;
            if (outbuf != 0 && outbuf < 0x80000000) {
                if (outbuf < 0x01800000 || (outbuf >= 0x10000000 && outbuf < 0x14000000))
                    outbuf |= 0x80000000;
            }
            if (outbuf >= 0x80000000 && outbuf < 0x94000000) {
                for (int i = 0; i < 32; i++) ctx.mmu.write8(outbuf + i, 0);
                ctx.mmu.write8(outbuf + 0, 0x01); // CD/DVD
                ctx.mmu.write8(outbuf + 1, 0x00);
                ctx.mmu.write8(outbuf + 8,  'R'); ctx.mmu.write8(outbuf + 9,  'V');
                ctx.mmu.write8(outbuf + 10, 'L'); ctx.mmu.write8(outbuf + 11, '-');
                ctx.mmu.write8(outbuf + 12, 'D'); ctx.mmu.write8(outbuf + 13, 'I');
                std::cout << "[DI] DVDLowInquiry: wrote RVL-DI to 0x" << std::hex << outbuf << std::dec << std::endl;
            }
            return IPC_OK;
        } else if (cmd == 0x8E) { // DI_ReadDiskID
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
                ctx.mmu.write32(buf + 0x18, 0x5D1C9EA3); // Wii magic
                std::cout << "[DI] ReadDiskID: wrote " << gid << " to 0x" << std::hex << buf << std::dec << std::endl;
            }
            return 1;
        } else if (cmd == 0x80) { // DI_Reset
            std::cout << "[DI] Reset acknowledged" << std::endl;
            return 1;
        } else if (cmd == 0x20) { // DI_Read
            // Temporary delegation until di_read_internal is moved completely
            di_read_internal_shared(&ctx.mmu, req.in_buf, 0, 0, false);
            return IPC_OK;
        } else if (cmd == 0x86) { // DI_ClearCoverInterrupt
            return IPC_OK;
        } else if (cmd == 0xE0) { // DVDGetCoverRegister
            return 1;
        } else if (cmd == 0x60) { // DVDGetError / StopMotor
            return IPC_OK;
        }

        std::cout << "[DI] Unhandled ioctl: 0x" << std::hex << cmd << std::dec << std::endl;
        return 1;
    }
};

std::unique_ptr<IDevice> create_di_device() {
    return std::make_unique<DIDevice>();
}

} // namespace nwii::runtime::devices

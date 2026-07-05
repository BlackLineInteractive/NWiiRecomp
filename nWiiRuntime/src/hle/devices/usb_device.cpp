#include "runtime/devices.h"
#include <iostream>

namespace nwii::runtime::devices {

// Minimal Bluetooth HCI emulation for /dev/usb/oh1 (the Wii Bluetooth
// host controller). The WPAD library runs a worker thread that submits
// HCI commands via USB control URBs and waits on interrupt URBs for the
// resulting events; without an event reply that thread sleeps forever.
// We answer each command with a "Command Complete" event on the next
// interrupt URB so the WPAD state machine advances.
class USBDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/usb"; }
    bool matches_path(const std::string& path) const override {
        return path.find("/dev/usb") == 0;
    }

    int32_t ioctl(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[USB] ioctl cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }

    int32_t ioctlv(CPUContext& ctx, const IpcRequest& req) override {
        // USBV0 opcodes: 11 = submit control URB, 13 = submit interrupt URB
        if (req.ioctl_cmd == 11) { // CTRL URB carries the HCI command
            if (req.ioctlv_vecs.size() >= 2) {
                uint32_t data_ptr = req.ioctlv_vecs[1].addr;
                uint32_t data_len = req.ioctlv_vecs[1].len;
                if (data_ptr != 0 && data_len >= 2) {
                    uint8_t lsb = ctx.mmu.read8(data_ptr);
                    uint8_t msb = ctx.mmu.read8(data_ptr + 1);
                    m_last_hci_opcode = (uint16_t)((msb << 8) | lsb);
                    std::cout << "[USB] HCI command opcode 0x" << std::hex
                              << m_last_hci_opcode << std::dec << std::endl;
                }
            }
            return IPC_OK;
        }

        if (req.ioctl_cmd == 13) { // INTR URB collects the HCI event
            // First output vector holds the event buffer
            uint32_t out_idx = req.arg_cnt_in;
            if (out_idx < req.ioctlv_vecs.size()) {
                uint32_t out_ptr = req.ioctlv_vecs[out_idx].addr;
                uint32_t out_len = req.ioctlv_vecs[out_idx].len;
                if (out_ptr != 0 && out_len >= 6) {
                    write_command_complete(ctx, out_ptr);
                }
            }
            return IPC_OK;
        }

        std::cout << "[USB] ioctlv cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }

private:
    uint16_t m_last_hci_opcode = 0;

    // Build a HCI "Command Complete" event for the last opcode
    void write_command_complete(CPUContext& ctx, uint32_t p) {
        ctx.mmu.write8(p + 0, 0x0E); // event code: Command Complete
        ctx.mmu.write8(p + 2, 0x01); // num HCI command packets
        ctx.mmu.write8(p + 3, m_last_hci_opcode & 0xFF);
        ctx.mmu.write8(p + 4, m_last_hci_opcode >> 8);
        ctx.mmu.write8(p + 5, 0x00); // status: success

        if (m_last_hci_opcode == 0x1001) {      // Read Local Version Info
            ctx.mmu.write8(p + 1, 0x0C);        // parameter length
            ctx.mmu.write8(p + 6, 0x06);        // HCI version (2.1)
            ctx.mmu.write8(p + 10, 0x0F);       // manufacturer LSB
        } else if (m_last_hci_opcode == 0x1009) { // Read BD_ADDR
            ctx.mmu.write8(p + 1, 0x0A);        // parameter length
            ctx.mmu.write8(p + 6, 0x11);
            ctx.mmu.write8(p + 7, 0x22);
            ctx.mmu.write8(p + 8, 0x33);
            ctx.mmu.write8(p + 9, 0x44);
            ctx.mmu.write8(p + 10, 0x55);
            ctx.mmu.write8(p + 11, 0x66);
        } else {
            ctx.mmu.write8(p + 1, 0x04);        // generic parameter length
        }
    }
};

std::unique_ptr<IDevice> create_usb_device() {
    return std::make_unique<USBDevice>();
}

} // namespace nwii::runtime::devices

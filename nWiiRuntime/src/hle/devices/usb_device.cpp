#include "runtime/devices.h"
#include "runtime/hw/hw.h"
#include <cstdint>
#include <deque>
#include <iostream>
#include <vector>

namespace nwii::runtime::devices {

// Bluetooth HCI emulation for /dev/usb/oh1 (the Wii Bluetooth host
// controller). The BTE stack (btu_init_core) drives a fixed HCI bring-up
// sequence: it submits each command on a USB *control* URB and then waits on
// *interrupt* URBs for the controller's event reply. Without an event the
// stack spins forever, so every command is answered on the event queue and
// handed back on the next interrupt URB.
//
// IOS USBV0 ioctlv opcodes for this device:
//   0 = CTRLMSG (6 in, 1 out) -- out vector carries the HCI command
//   1 = BLKMSG  (2 in, 1 out) -- ACL data
//   2 = INTRMSG (2 in, 1 out) -- out vector receives the HCI event
namespace {
constexpr uint32_t USBV0_CTRLMSG = 0;
constexpr uint32_t USBV0_BLKMSG = 1;
constexpr uint32_t USBV0_INTRMSG = 2;

constexpr uint8_t HCI_EVT_COMMAND_COMPLETE = 0x0E;
constexpr uint8_t HCI_EVT_COMMAND_STATUS = 0x0F;
} // namespace

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
        // The output/payload buffer is nominally the vector at index
        // arg_cnt_in, but this stack frequently leaves that slot zeroed and
        // carries the real buffer in another vector. Scan from the end for
        // the last plausible pointer with a usable length instead.
        uint32_t out_ptr = 0, out_len = 0;
        for (size_t i = req.ioctlv_vecs.size(); i-- > 0;) {
            const auto& v = req.ioctlv_vecs[i];
            if (v.addr >= 0x100 && v.len >= 2 && v.len < 0x10000) {
                out_ptr = v.addr;
                out_len = v.len;
                break;
            }
        }

        switch (req.ioctl_cmd) {
        case USBV0_CTRLMSG: {
            // The BTE stack passes the control-transfer setup fields and the
            // payload as separate vectors, but their order and the pointer
            // form (physical vs. virtual) vary between call sites. Instead of
            // trusting an index, scan every vector and validate its bytes as
            // an HCI command packet (opcode LE u16 with a sane OGF, third
            // byte = parameter length). The MMU masks both address forms.
            for (size_t i = req.ioctlv_vecs.size(); i-- > 0;) {
                uint32_t p = req.ioctlv_vecs[i].addr;
                if (p == 0 || p < 0x100)
                    continue; // tiny values are inline setup fields, not pointers
                uint16_t opcode = (uint16_t)(ctx.mmu.read8(p) |
                                             (ctx.mmu.read8(p + 1) << 8));
                uint16_t ogf = opcode >> 10;
                if (opcode == 0 || opcode == 0xFFFF || ogf == 0 || ogf > 0x3F)
                    continue;
                handle_hci_command(ctx, p, 3u + ctx.mmu.read8(p + 2));
                break;
            }
            return IPC_OK;
        }

        case USBV0_INTRMSG: {
            // Real IOS keeps an IN URB pending until the endpoint produces
            // data. Completing it immediately with zero bytes makes the BTE
            // stack count a command timeout and abandon its init sequence,
            // so with no event queued we defer the reply instead.
            if (!m_events.empty())
                return deliver_event(ctx, out_ptr, out_len);
            if (req.req_addr != 0 && out_ptr != 0) {
                m_pending_intr.push_back({req.req_addr, out_ptr, out_len});
                return IPC_NO_REPLY;
            }
            return IPC_OK;
        }

        case USBV0_BLKMSG: {
            // Bulk IN (endpoint bit 0x80) carries ACL data from a paired
            // remote; none exists, so the URB stays pending forever like on
            // hardware with no Wiimote connected. Bulk OUT completes at once.
            uint8_t ep = endpoint_of(ctx, req);
            if ((ep & 0x80) && req.req_addr != 0)
                return IPC_NO_REPLY;
            return IPC_OK;
        }

        default:
            std::cout << "[USB] ioctlv cmd=" << req.ioctl_cmd << std::endl;
            return IPC_OK;
        }
    }

private:
    std::deque<std::vector<uint8_t>> m_events;

    // Interrupt-IN URBs parked until an HCI event exists (real-IOS behaviour).
    struct PendingRead { uint32_t req_addr, ptr, len; };
    std::deque<PendingRead> m_pending_intr;

    // bEndpointAddress travels as the request's single-byte vector.
    static uint8_t endpoint_of(CPUContext& ctx, const IpcRequest& req) {
        for (const auto& v : req.ioctlv_vecs)
            if (v.len == 1 && v.addr >= 0x100)
                return ctx.mmu.read8(v.addr);
        return 0;
    }

    // HCI command packet: opcode (LE u16), parameter length, parameters.
    void handle_hci_command(CPUContext& ctx, uint32_t p, uint32_t len) {
        uint16_t opcode = (uint16_t)(ctx.mmu.read8(p) | (ctx.mmu.read8(p + 1) << 8));
        std::cout << "[USB] HCI command opcode 0x" << std::hex << opcode
                  << std::dec << std::endl;

        // OGF 0x01 (Link Control) commands complete asynchronously and are
        // acknowledged with Command Status instead of Command Complete.
        if ((opcode >> 10) == 0x01)
            m_events.push_back(make_command_status(opcode));
        else
            m_events.push_back(make_command_complete(opcode));

        // Wake a parked interrupt URB with the new event. Only one reply can
        // be in flight per IPC tick; further events go out on later reads.
        if (!m_pending_intr.empty()) {
            PendingRead pr = m_pending_intr.front();
            m_pending_intr.pop_front();
            int32_t n = deliver_event(ctx, pr.ptr, pr.len);
            ctx.mmu.write32(pr.req_addr + 4, (uint32_t)n);
            nwii::runtime::hw::ipc_post_reply(pr.req_addr);
        }
    }

    // Returns the number of bytes handed to the guest, or 0 when idle.
    int32_t deliver_event(CPUContext& ctx, uint32_t p, uint32_t len) {
        if (m_events.empty() || p == 0 || len == 0)
            return IPC_OK;
        std::vector<uint8_t> evt = std::move(m_events.front());
        m_events.pop_front();
        uint32_t n = (uint32_t)evt.size() < len ? (uint32_t)evt.size() : len;
        for (uint32_t i = 0; i < n; ++i)
            ctx.mmu.write8(p + i, evt[i]);
        if (std::getenv("NWII_SAMPLE")) {
            std::cout << "[USB] event -> 0x" << std::hex << p << " len=" << std::dec
                      << n << " bytes:";
            for (uint32_t i = 0; i < n && i < 8; ++i)
                std::cout << " " << std::hex << (int)evt[i];
            std::cout << std::dec << "\n";
        }
        return (int32_t)n;
    }

    static std::vector<uint8_t> make_command_status(uint16_t opcode) {
        return {HCI_EVT_COMMAND_STATUS, 0x04, 0x00, 0x01,
                (uint8_t)(opcode & 0xFF), (uint8_t)(opcode >> 8)};
    }

    // Command Complete: [0]=0x0E [1]=param_len [2]=num_cmd_pkts [3..4]=opcode
    // [5]=status [6..]=return parameters. param_len = 4 + extra parameters.
    static std::vector<uint8_t> make_command_complete(uint16_t opcode) {
        std::vector<uint8_t> ret = return_parameters(opcode);
        std::vector<uint8_t> e = {HCI_EVT_COMMAND_COMPLETE,
                                  (uint8_t)(4 + ret.size()),
                                  0x01,
                                  (uint8_t)(opcode & 0xFF),
                                  (uint8_t)(opcode >> 8),
                                  0x00}; // status: success
        e.insert(e.end(), ret.begin(), ret.end());
        return e;
    }

    // Return parameters that follow the status byte, per HCI command.
    static std::vector<uint8_t> return_parameters(uint16_t opcode) {
        switch (opcode) {
        case 0x1001: // Read Local Version Information
            return {0x06,       // HCI version (2.1 + EDR)
                    0x00, 0x00, // HCI revision
                    0x06,       // LMP version
                    0x0F, 0x00, // manufacturer (Broadcom)
                    0x00, 0x00}; // LMP subversion
        case 0x1002: // Read Local Supported Commands (64-byte bitmap)
            return std::vector<uint8_t>(64, 0xFF);
        case 0x1003: // Read Local Supported Features (8-byte bitmap)
            return {0xFF, 0xFF, 0x8F, 0xFE, 0x9B, 0xFD, 0x00, 0x80};
        case 0x1005: // Read Buffer Size
            return {0x39, 0x00, // ACL max packet length (57)
                    0x40,       // SCO max packet length (64)
                    0x0A, 0x00, // ACL max packets (10)
                    0x08, 0x00}; // SCO max packets (8)
        case 0x1009: // Read BD_ADDR
            return {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        case 0x0C23: // Read Class of Device
            return {0x00, 0x04, 0x48};
        case 0x0C14: // Read Local Name (248-byte, NUL padded)
            return std::vector<uint8_t>(248, 0x00);
        default:
            return {}; // bare success
        }
    }
};

std::unique_ptr<IDevice> create_usb_device() {
    return std::make_unique<USBDevice>();
}

} // namespace nwii::runtime::devices

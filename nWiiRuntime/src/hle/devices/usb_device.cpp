#include "runtime/devices.h"
#include "runtime/hw/hw.h"
#include <cstdint>
#include <deque>
#include <iostream>
#include <vector>

namespace nwii::runtime::devices {








namespace {
constexpr uint32_t USBV0_CTRLMSG = 0;
constexpr uint32_t USBV0_BLKMSG = 1;
constexpr uint32_t USBV0_INTRMSG = 2;

constexpr uint8_t HCI_EVT_COMMAND_COMPLETE = 0x0E;
constexpr uint8_t HCI_EVT_COMMAND_STATUS = 0x0F;
} 

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

            

            
            for (size_t i = req.ioctlv_vecs.size(); i-- > 0;) {
                uint32_t p = req.ioctlv_vecs[i].addr;
                if (p == 0 || p < 0x100)
                    continue; 
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

            
            
            if (!m_events.empty())
                return deliver_event(ctx, out_ptr, out_len);
            if (req.req_addr != 0 && out_ptr != 0) {
                m_pending_intr.push_back({req.req_addr, out_ptr, out_len});
                return IPC_NO_REPLY;
            }
            return IPC_OK;
        }

        case USBV0_BLKMSG: {

            
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

    struct PendingRead { uint32_t req_addr, ptr, len; };
    std::deque<PendingRead> m_pending_intr;

    static uint8_t endpoint_of(CPUContext& ctx, const IpcRequest& req) {
        for (const auto& v : req.ioctlv_vecs)
            if (v.len == 1 && v.addr >= 0x100)
                return ctx.mmu.read8(v.addr);
        return 0;
    }

    void handle_hci_command(CPUContext& ctx, uint32_t p, uint32_t len) {
        uint16_t opcode = (uint16_t)(ctx.mmu.read8(p) | (ctx.mmu.read8(p + 1) << 8));
        std::cout << "[USB] HCI command opcode 0x" << std::hex << opcode
                  << std::dec << std::endl;

        
        if ((opcode >> 10) == 0x01)
            m_events.push_back(make_command_status(opcode));
        else
            m_events.push_back(make_command_complete(opcode));

        
        if (!m_pending_intr.empty()) {
            PendingRead pr = m_pending_intr.front();
            m_pending_intr.pop_front();
            int32_t n = deliver_event(ctx, pr.ptr, pr.len);
            ctx.mmu.write32(pr.req_addr + 4, (uint32_t)n);
            nwii::runtime::hw::ipc_post_reply(pr.req_addr);
        }
    }

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

    
    static std::vector<uint8_t> make_command_complete(uint16_t opcode) {
        std::vector<uint8_t> ret = return_parameters(opcode);
        std::vector<uint8_t> e = {HCI_EVT_COMMAND_COMPLETE,
                                  (uint8_t)(4 + ret.size()),
                                  0x01,
                                  (uint8_t)(opcode & 0xFF),
                                  (uint8_t)(opcode >> 8),
                                  0x00}; 
        e.insert(e.end(), ret.begin(), ret.end());
        return e;
    }

    static std::vector<uint8_t> return_parameters(uint16_t opcode) {
        switch (opcode) {
        case 0x1001: 
            return {0x06,       
                    0x00, 0x00, 
                    0x06,       
                    0x0F, 0x00, 
                    0x00, 0x00}; 
        case 0x1002: 
            return std::vector<uint8_t>(64, 0xFF);
        case 0x1003: 
            return {0xFF, 0xFF, 0x8F, 0xFE, 0x9B, 0xFD, 0x00, 0x80};
        case 0x1005: 
            return {0x39, 0x00, 
                    0x40,       
                    0x0A, 0x00, 
                    0x08, 0x00}; 
        case 0x1009: 
            return {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        case 0x0C23: 
            return {0x00, 0x04, 0x48};
        case 0x0C14: 
            return std::vector<uint8_t>(248, 0x00);
        default:
            return {}; 
        }
    }
};

std::unique_ptr<IDevice> create_usb_device() {
    return std::make_unique<USBDevice>();
}

} 

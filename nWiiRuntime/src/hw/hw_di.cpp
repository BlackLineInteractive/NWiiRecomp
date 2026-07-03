#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include "runtime/virtual_disc.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <vector>

namespace nwii::runtime {
    extern MMU* g_mmu;
}

namespace nwii::runtime::hw {

// GC/Wii DI registers (Dolphin DVDInterface):
//   0x00 DISR    bit0 BRK(w1c) bit1 DEINTMASK bit2 DEINT(w1c)
//                bit3 TCINTMASK bit4 TCINT(w1c) bit5 BRKINTMASK
//   0x04 DICVR   bit0 CVR(ro, 0=cover closed) bit1 CVRINTMASK bit2 CVRINT(w1c)
//   0x08..0x10   DICMDBUF0..2
//   0x14 DIMAR   0x18 DILENGTH   0x1C DICR   0x20 DIIMMBUF   0x24 DICFG
static uint32_t di_cmd[3] = {0, 0, 0};
static uint32_t di_mar = 0;
static uint32_t di_len = 0;
static uint32_t di_cr = 0;
static uint32_t di_imm = 0;
static uint32_t di_sr = 0;
static uint32_t di_cvr = 0;

static constexpr uint32_t SR_BRK = 0x01, SR_DEINTMASK = 0x02, SR_DEINT = 0x04;
static constexpr uint32_t SR_TCINTMASK = 0x08, SR_TCINT = 0x10, SR_BRKINTMASK = 0x20;
static constexpr uint32_t CVR_CVRINTMASK = 0x02, CVR_CVRINT = 0x04;

static void di_update_interrupt() {
    bool assert_int = ((di_sr & SR_TCINT) && (di_sr & SR_TCINTMASK)) ||
                      ((di_sr & SR_DEINT) && (di_sr & SR_DEINTMASK)) ||
                      ((di_sr & SR_BRK) && (di_sr & SR_BRKINTMASK)) ||
                      ((di_cvr & CVR_CVRINT) && (di_cvr & CVR_CVRINTMASK));
    if (assert_int)
        trigger_pi_interrupt(0x04); // DI = PI_INTSR bit 2
    else
        clear_pi_interrupt(0x04);
}

// Execute the drive command latched in DICMDBUF0..2 (DICR TSTART written)
static void di_execute() {
    uint32_t cmd = di_cmd[0] >> 24;

    switch (cmd) {
    case 0xA8: { // Read: DICMDBUF1 = offset>>2, DILENGTH = byte count
        auto& vd = VirtualDisc::get();
        if (!vd.valid())
            vd.init(nwii::runtime::Config::get().game_dir);
        uint64_t offset = (uint64_t)di_cmd[1] << 2;
        uint32_t length = di_len ? di_len : di_cmd[2];
        uint32_t dst = di_mar | 0x80000000;
        if (nwii::runtime::g_mmu && length > 0 && length < 0x4000000) {
            std::vector<uint8_t> tmp(length, 0);
            if (vd.valid())
                vd.read(offset, length, tmp.data());
            for (uint32_t i = 0; i < length; ++i)
                nwii::runtime::g_mmu->write8(dst + i, tmp[i]);
            std::cout << "[HW DI] DMA read off=0x" << std::hex << offset
                      << " len=0x" << length << " dst=0x" << dst << std::dec
                      << std::endl;
        }
        di_len = 0;
        break;
    }
    case 0x12: { // Inquiry: DMA 0x20 bytes of drive info
        if (nwii::runtime::g_mmu && di_len > 0 && di_len <= 0x40) {
            uint32_t dst = di_mar | 0x80000000;
            for (uint32_t i = 0; i < di_len; ++i)
                nwii::runtime::g_mmu->write8(dst + i, 0);
            if (di_len >= 8) {
                // Retail GC drive revision 0x20020402
                nwii::runtime::g_mmu->write32(dst + 4, 0x20020402);
            }
        }
        di_len = 0;
        break;
    }
    case 0xAB: // Seek
    case 0xE0: // RequestError: 0 = no error
    case 0xE1: // PlayAudio (streaming)
    case 0xE2: // RequestAudioStatus
    case 0xE3: // StopMotor
    case 0xE4: // AudioBufferConfig
        di_imm = 0;
        break;
    default:
        std::cout << "[HW DI] Unhandled drive command 0x" << std::hex << cmd
                  << std::dec << "\n";
        di_imm = 0;
        break;
    }

    di_cr &= ~1;         // TSTART self-clears
    di_sr |= SR_TCINT;   // transfer complete
    di_update_interrupt();
}

void register_di(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC006000, 0xCC0060FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC006000) return di_sr;
            if (addr == 0xCC006004) return di_cvr; // bit0=0: cover closed
            if (addr == 0xCC006008) return di_cmd[0];
            if (addr == 0xCC00600C) return di_cmd[1];
            if (addr == 0xCC006010) return di_cmd[2];
            if (addr == 0xCC006014) return di_mar;
            if (addr == 0xCC006018) return di_len;
            if (addr == 0xCC00601C) return di_cr;
            if (addr == 0xCC006020) return di_imm;
            if (addr == 0xCC006024) return 0x000000FF; // DICFG
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC006000) {
                // Status bits are write-1-to-clear, mask bits plain writes
                uint32_t w1c = val & (SR_BRK | SR_DEINT | SR_TCINT);
                uint32_t masks = SR_DEINTMASK | SR_TCINTMASK | SR_BRKINTMASK;
                di_sr = ((di_sr & ~w1c) & ~masks) | (val & masks);
                di_update_interrupt();
            } else if (addr == 0xCC006004) {
                uint32_t w1c = val & CVR_CVRINT;
                di_cvr = ((di_cvr & ~w1c) & ~CVR_CVRINTMASK) | (val & CVR_CVRINTMASK);
                di_update_interrupt();
            }
            else if (addr == 0xCC006008) { di_cmd[0] = val; }
            else if (addr == 0xCC00600C) { di_cmd[1] = val; }
            else if (addr == 0xCC006010) { di_cmd[2] = val; }
            else if (addr == 0xCC006014) { di_mar = val; }
            else if (addr == 0xCC006018) { di_len = val; }
            else if (addr == 0xCC00601C) {
                di_cr = val;
                if (val & 1) di_execute();
            }
            else if (addr == 0xCC006020) { di_imm = val; }
        }
    );
}}

} // namespace nwii::runtime::hw

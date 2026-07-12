#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include "input/input_manager.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

// SI registers (Dolphin SerialInterface):
//   0xCC006400 + ch*0x0C: SIC[n]OUTBUF, SIC[n]INBUFH, SIC[n]INBUFL
//   0xCC006430 SIPOLL, 0xCC006434 SICOMCSR, 0xCC006438 SISR,
//   0xCC00643C SIEXICLK, 0xCC006480..0xCC0064FF SI IO buffer (128 bytes)
struct SIChannel {
  uint32_t out = 0;
};
static SIChannel si_chan[4];
static uint32_t si_poll = 0;
static uint32_t si_com_csr = 0;
static uint32_t si_status = 0;
static uint32_t si_exi_clock = 0;
static uint8_t si_io_buf[128];

// Build the 8-byte GC pad poll response (command 0x40, mode 3 packing)
static void pad_response(int chan, uint32_t& hi, uint32_t& lo) {
  using nwii::runtime::input::InputManager;
  auto state = InputManager::get().get_gcpad_state(chan);

  // byte0: 0 0 0 Start Y X B A ; byte1: 1 L R Z Dup Ddown Dright Dleft
  // Raw joybus sticks are unsigned centered at 0x80; InputManager stores
  // PAD-library signed values centered at 0, so shift by 0x80 here.
  uint8_t b0 = (state.buttons >> 8) & 0x1F;
  uint8_t b1 = 0x80 | (state.buttons & 0x7F);
  uint8_t sx = (uint8_t)(state.stick_x + 0x80);
  uint8_t sy = (uint8_t)(state.stick_y + 0x80);
  uint8_t cx = (uint8_t)(state.substick_x + 0x80);
  uint8_t cy = (uint8_t)(state.substick_y + 0x80);
  hi = ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)sx << 8) | sy;
  lo = ((uint32_t)cx << 24) | ((uint32_t)cy << 16) |
       ((uint32_t)state.trigger_l << 8) | state.trigger_r;
}

// Execute a joybus command transfer via the SI IO buffer (SICOMCSR TSTART)
static void si_transfer() {
  uint32_t chan = (si_com_csr >> 1) & 3;
  uint8_t cmd = si_io_buf[0];

  std::memset(si_io_buf, 0, sizeof(si_io_buf));

  bool connected = (chan == 0); // controller in port 1 only

  switch (cmd) {
  case 0x00: // Reset / request ID
  case 0xFF:
    if (connected) {
      si_io_buf[0] = 0x09; // GC standard controller ID = 0x0900
      si_io_buf[1] = 0x00;
      si_io_buf[2] = 0x00;
    } else {
      // No device: NOREP error for this channel
      si_status |= (uint32_t)0x08 << ((3 - chan) * 8);
    }
    break;
  case 0x40: { // Direct poll
    uint32_t hi = 0, lo = 0;
    if (connected) pad_response(chan, hi, lo);
    si_io_buf[0] = hi >> 24; si_io_buf[1] = hi >> 16;
    si_io_buf[2] = hi >> 8;  si_io_buf[3] = hi;
    si_io_buf[4] = lo >> 24; si_io_buf[5] = lo >> 16;
    si_io_buf[6] = lo >> 8;  si_io_buf[7] = lo;
    break;
  }
  case 0x41: // Get origin
  case 0x42: // Recalibrate
    if (connected) {
      si_io_buf[0] = 0x00; si_io_buf[1] = 0x80; // neutral buttons
      si_io_buf[2] = 0x80; si_io_buf[3] = 0x80; // main stick center
      si_io_buf[4] = 0x80; si_io_buf[5] = 0x80; // c-stick center
      si_io_buf[6] = 0x00; si_io_buf[7] = 0x00; // triggers released
      si_io_buf[8] = 0x00; si_io_buf[9] = 0x00; // analog A/B
    } else {
      si_status |= (uint32_t)0x08 << ((3 - chan) * 8);
    }
    break;
  default:
    if (!connected)
      si_status |= (uint32_t)0x08 << ((3 - chan) * 8);
    break;
  }

  si_com_csr &= ~1;          // TSTART self-clears
  si_com_csr |= 0x80000000;  // TCINT: transfer complete
  if (si_com_csr & 0x40000000) // TCINTMSK
    trigger_pi_interrupt(0x08); // SI = PI bit 3
}

void register_si(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC006400, 0xCC0064FF,
        [](uint32_t addr) -> uint32_t {
            if (addr <= 0xCC00642C) {
                int ch = (addr - 0xCC006400) / 0x0C;
                int reg = (addr - 0xCC006400) % 0x0C;
                if (ch >= 0 && ch < 4) {
                    if (reg == 0x00) return si_chan[ch].out;
                    if (reg == 0x04) {
                        // Reading INBUFH clears this channel's RDST
                        si_status &= ~((uint32_t)0x20 << ((3 - ch) * 8));
                        if (ch == 0) {
                            uint32_t hi = 0, lo = 0;
                            pad_response(0, hi, lo);
                            return hi & 0x3FFFFFFF; // ERRSTAT/ERRLATCH clear
                        }
                        return 0x80000000; // no controller: ERRSTAT
                    }
                    if (reg == 0x08) {
                        if (ch == 0) {
                            uint32_t hi = 0, lo = 0;
                            pad_response(0, hi, lo);
                            return lo;
                        }
                        return 0;
                    }
                }
            }
            if (addr == 0xCC006430) return si_poll;
            if (addr == 0xCC006434) return si_com_csr;
            if (addr == 0xCC006438) {
                // Polling enabled (SIPOLL EN bits 7-4): report RDST for ch0
                if (si_poll & 0x80)
                    si_status |= 0x20000000; // RDST channel 0
                return si_status;
            }
            if (addr == 0xCC00643C) return si_exi_clock;
            if (addr >= 0xCC006480 && addr < 0xCC006500) {
                uint32_t off = addr - 0xCC006480;
                return ((uint32_t)si_io_buf[off] << 24) |
                       ((uint32_t)si_io_buf[off + 1] << 16) |
                       ((uint32_t)si_io_buf[off + 2] << 8) | si_io_buf[off + 3];
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr <= 0xCC00642C) {
                int ch = (addr - 0xCC006400) / 0x0C;
                int reg = (addr - 0xCC006400) % 0x0C;
                if (ch >= 0 && ch < 4 && reg == 0x00) si_chan[ch].out = val;
                return;
            }
            if (addr == 0xCC006430) { si_poll = val; return; }
            if (addr == 0xCC006434) {
                if (std::getenv("NWII_SAMPLE")) std::cout << "[HW SI] Write to si_com_csr: 0x" << std::hex << val << std::dec << "\n";
                // Bits 31 (TCINT), 29 (RDSTINT), 28 (WRSTINT) are W1C
                si_com_csr &= ~(val & 0xA0000000);
                si_com_csr |= (val & ~0x80000000);
                if (val & 0x80000000) {
                    si_com_csr &= ~0x80000000; // TCINT w1c
                    clear_pi_interrupt(0x08); // SI = PI bit 3
                }
                if (val & 1) si_transfer();
                return;
            }
            if (addr == 0xCC006438) {
                uint32_t clear_mask = val & 0x0F0F0F0F;
                si_status &= ~clear_mask;
                if (val & 0x80000000) {
                    // WR: latch output buffers (no-op in HLE)
                    si_status &= ~0x80000000;
                }
                return;
            }
            if (addr == 0xCC00643C) { si_exi_clock = val; return; }
            if (addr >= 0xCC006480 && addr < 0xCC006500) {
                uint32_t off = addr - 0xCC006480;
                si_io_buf[off] = val >> 24;
                si_io_buf[off + 1] = val >> 16;
                si_io_buf[off + 2] = val >> 8;
                si_io_buf[off + 3] = val;
                return;
            }
        }
    );
}}

} // namespace nwii::runtime::hw

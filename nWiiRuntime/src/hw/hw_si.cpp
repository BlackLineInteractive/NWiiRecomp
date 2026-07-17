#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include "input/input_manager.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

// SI registers (Dolphin SerialInterface):


struct SIChannel {
  uint32_t out = 0;
};
static SIChannel si_chan[4];
static uint32_t si_poll = 0;
static uint32_t si_com_csr = 0;
static uint32_t si_status = 0;
static uint32_t si_exi_clock = 0;
static uint8_t si_io_buf[128];

static void pad_response(int chan, uint32_t& hi, uint32_t& lo) {
  using nwii::runtime::input::InputManager;
  auto state = InputManager::get().get_gcpad_state(chan);

  
  
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

static void si_transfer() {
  uint32_t chan = (si_com_csr >> 1) & 3;
  uint8_t cmd = si_io_buf[0];

  std::memset(si_io_buf, 0, sizeof(si_io_buf));

  bool connected = (chan == 0); 

  switch (cmd) {
  case 0x00: 
  case 0xFF:
    if (connected) {
      si_io_buf[0] = 0x09; 
      si_io_buf[1] = 0x00;
      si_io_buf[2] = 0x00;
    } else {
      
      si_status |= (uint32_t)0x08 << ((3 - chan) * 8);
    }
    break;
  case 0x40: { 
    uint32_t hi = 0, lo = 0;
    if (connected) pad_response(chan, hi, lo);
    si_io_buf[0] = hi >> 24; si_io_buf[1] = hi >> 16;
    si_io_buf[2] = hi >> 8;  si_io_buf[3] = hi;
    si_io_buf[4] = lo >> 24; si_io_buf[5] = lo >> 16;
    si_io_buf[6] = lo >> 8;  si_io_buf[7] = lo;
    break;
  }
  case 0x41: 
  case 0x42: 
    if (connected) {
      si_io_buf[0] = 0x00; si_io_buf[1] = 0x80; 
      si_io_buf[2] = 0x80; si_io_buf[3] = 0x80; 
      si_io_buf[4] = 0x80; si_io_buf[5] = 0x80; 
      si_io_buf[6] = 0x00; si_io_buf[7] = 0x00; 
      si_io_buf[8] = 0x00; si_io_buf[9] = 0x00; 
    } else {
      si_status |= (uint32_t)0x08 << ((3 - chan) * 8);
    }
    break;
  default:
    if (!connected)
      si_status |= (uint32_t)0x08 << ((3 - chan) * 8);
    break;
  }

  si_com_csr &= ~1;          
  si_com_csr |= 0x80000000;  
  if (si_com_csr & 0x40000000) 
    trigger_pi_interrupt(0x08); 
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
                        
                        si_status &= ~((uint32_t)0x20 << ((3 - ch) * 8));
                        if (ch == 0) {
                            uint32_t hi = 0, lo = 0;
                            pad_response(0, hi, lo);
                            return hi & 0x3FFFFFFF; 
                        }
                        return 0x80000000; 
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
                
                if (si_poll & 0x80)
                    si_status |= 0x20000000; 
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
                
                si_com_csr &= ~(val & 0xA0000000);
                si_com_csr |= (val & ~0x80000000);
                if (val & 0x80000000) {
                    si_com_csr &= ~0x80000000; 
                    clear_pi_interrupt(0x08); 
                }
                if (val & 1) si_transfer();
                return;
            }
            if (addr == 0xCC006438) {
                uint32_t clear_mask = val & 0x0F0F0F0F;
                si_status &= ~clear_mask;
                if (val & 0x80000000) {
                    
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

} 

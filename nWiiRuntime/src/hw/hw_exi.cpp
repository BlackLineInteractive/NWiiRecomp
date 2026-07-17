#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace nwii::runtime {
    extern MMU* g_mmu;
}

namespace nwii::runtime::hw {





struct EXIChannel {
  uint32_t csr = 0;
  uint32_t mar = 0;
  uint32_t len = 0;
  uint32_t cr = 0;
  uint32_t data = 0;
};
static EXIChannel exi_chan[3];



static uint32_t mx_command = 0;
static uint32_t mx_position = 0;
static uint8_t sram[64];
static bool sram_init_done = false;

static void sram_init() {
    if (sram_init_done) return;
    sram_init_done = true;
    std::memset(sram, 0, sizeof(sram));
    
    sram[0x12] = 0;    
    sram[0x13] = 0x40; 
    
    uint16_t sum = 0, inv = 0;
    for (int off = 0x04; off < 0x14; off += 2) {
        uint16_t w = ((uint16_t)sram[off] << 8) | sram[off + 1];
        sum += w;
        inv += (uint16_t)~w;
    }
    sram[0] = sum >> 8; sram[1] = sum & 0xFF;
    sram[2] = inv >> 8; sram[3] = inv & 0xFF;
}

static uint32_t rtc_counter() {
    return (uint32_t)(std::time(nullptr) - 946684800);
}

static uint8_t mx_read_byte() {
    uint32_t addr = mx_command & 0x7FFFFF00;
    uint32_t off = mx_position++;
    if (addr == 0x20000000) { 
        uint32_t v = rtc_counter();
        return (uint8_t)(v >> (24 - (off & 3) * 8));
    }
    if (addr == 0x20000100) { 
        sram_init();
        return (off < sizeof(sram)) ? sram[off] : 0;
    }
    return 0; 
}

static void mx_write_byte(uint8_t v) {
    uint32_t addr = mx_command & 0x7FFFFF00;
    if (addr == 0x20000100) { 
        sram_init();
        uint32_t off = mx_position++;
        if (off < sizeof(sram)) sram[off] = v;
    } else {
        mx_position++;
    }
}

static int selected_device(uint32_t csr) {
    if (csr & 0x080) return 0;
    if (csr & 0x100) return 1;
    if (csr & 0x200) return 2;
    return -1;
}

static void exi_finish(int ch) {
    exi_chan[ch].cr &= ~1;      
    exi_chan[ch].csr |= 0x08;   
    if (exi_chan[ch].csr & 0x04) 
        trigger_pi_interrupt(0x10); 
}

static void exi_transfer(int ch) {
    uint32_t cr = exi_chan[ch].cr;
    bool dma = (cr & 2) != 0;
    uint32_t rw = (cr >> 2) & 3; 
    uint32_t tlen = ((cr >> 4) & 3) + 1;
    int dev = selected_device(exi_chan[ch].csr);
    bool is_mx = (ch == 0 && dev == 1);

    if (!dma) {
        if (rw == 1) { 
            if (is_mx) {
                if (tlen == 4) {
                    
                    mx_command = exi_chan[ch].data;
                    mx_position = 0;
                } else {
                    for (uint32_t i = 0; i < tlen; ++i)
                        mx_write_byte((uint8_t)(exi_chan[ch].data >> (24 - i * 8)));
                }
            }
        } else { 
            uint32_t v = 0;
            for (uint32_t i = 0; i < tlen; ++i) {
                uint8_t b = is_mx ? mx_read_byte() : 0xFF;
                v |= (uint32_t)b << (24 - i * 8);
            }
            exi_chan[ch].data = v;
        }
    } else if (nwii::runtime::g_mmu) {
        uint32_t addr = exi_chan[ch].mar | 0x80000000;
        uint32_t len = exi_chan[ch].len;
        if (len < 0x2000000) {
            if (rw == 0) { 
                for (uint32_t i = 0; i < len; ++i)
                    nwii::runtime::g_mmu->write8(addr + i,
                                                 is_mx ? mx_read_byte() : 0xFF);
            } else {       
                for (uint32_t i = 0; i < len; ++i) {
                    uint8_t b = nwii::runtime::g_mmu->read8(addr + i);
                    if (is_mx) mx_write_byte(b);
                }
            }
        }
    }

    exi_finish(ch);
}

void register_exi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC006800, 0xCC0068FF,
        [](uint32_t addr) -> uint32_t {
            if (addr > 0xCC00683C) return 0;
            int ch = (addr - 0xCC006800) / 0x14;
            int reg = (addr - 0xCC006800) % 0x14;
            if (ch >= 0 && ch < 3) {
                if (reg == 0x00) {

                    // chip — Dolphin EXI_Channel.cpp: "EXT =

                    

                    return exi_chan[ch].csr;
                }
                if (reg == 0x04) return exi_chan[ch].mar;
                if (reg == 0x08) return exi_chan[ch].len;
                if (reg == 0x0C) return exi_chan[ch].cr;
                if (reg == 0x10) return exi_chan[ch].data;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr > 0xCC00683C) return;
            int ch = (addr - 0xCC006800) / 0x14;
            int reg = (addr - 0xCC006800) % 0x14;
            if (ch < 0 || ch >= 3) return;
            if (reg == 0x00) {
                
                uint32_t w1c = val & 0x80A;
                uint32_t keep = exi_chan[ch].csr & ~0x80A & 0x1000;
                exi_chan[ch].csr =
                    keep | ((exi_chan[ch].csr & 0x80A & ~w1c)) |
                    (val & ~0x80A & ~0x1000);
                if (w1c & 0x02) clear_pi_interrupt(0x10); 
                if (w1c & 0x08) clear_pi_interrupt(0x10); 
            }
            else if (reg == 0x04) exi_chan[ch].mar = val;
            else if (reg == 0x08) exi_chan[ch].len = val;
            else if (reg == 0x0C) {
                exi_chan[ch].cr = val;
                if (val & 1) exi_transfer(ch);
            }
            else if (reg == 0x10) exi_chan[ch].data = val;
        }
    );
}}

} 

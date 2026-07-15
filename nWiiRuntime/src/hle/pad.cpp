#include "runtime/cpu_context.h"
#include "input/input_manager.h"

using namespace nwii::runtime;
using namespace nwii::runtime::input;

extern "C" {

void PADInit(CPUContext& ctx) {}
void WPADInit(CPUContext& ctx) {}
void KPADInit(CPUContext& ctx) {}

void PADRead(CPUContext& ctx) {
    uint32_t pad_status_array_addr = ctx.gpr[3];
    GameCubePadState state = InputManager::get().get_gcpad_state(0);

    ctx.mmu.write16(pad_status_array_addr, state.buttons);
    ctx.mmu.write8(pad_status_array_addr + 2, (uint8_t)state.stick_x);
    ctx.mmu.write8(pad_status_array_addr + 3, (uint8_t)state.stick_y);
    ctx.mmu.write8(pad_status_array_addr + 4, (uint8_t)state.substick_x);
    ctx.mmu.write8(pad_status_array_addr + 5, (uint8_t)state.substick_y);
    ctx.mmu.write8(pad_status_array_addr + 6, state.trigger_l);
    ctx.mmu.write8(pad_status_array_addr + 7, state.trigger_r);
    ctx.mmu.write8(pad_status_array_addr + 8, state.buttons & 0x0100 ? 0xFF : 0);
    ctx.mmu.write8(pad_status_array_addr + 9, state.buttons & 0x0200 ? 0xFF : 0);
    ctx.mmu.write8(pad_status_array_addr + 10, state.err);
    ctx.mmu.write8(pad_status_array_addr + 11, 0);

    for (int i = 1; i < 4; i++) {
        uint32_t offset = pad_status_array_addr + (i * 12);
        ctx.mmu.write16(offset, 0);
        ctx.mmu.write8(offset + 10, (uint8_t)-1); // PAD_ERR_NO_CONTROLLER
    }
}

void WPADRead(CPUContext& ctx) {
    int32_t chan = (int32_t)ctx.gpr[3];
    uint32_t data_ptr = ctx.gpr[4];

    if (chan >= 0 && chan < 4 && data_ptr != 0) {
        WiimoteState state = InputManager::get().get_wiimote_state(chan);
        ctx.mmu.write32(data_ptr + 0x00, state.err);
        ctx.mmu.write32(data_ptr + 0x04, state.buttons);
    }
    ctx.gpr[3] = 0; // WPAD_ERR_NONE
}

void KPADRead(CPUContext& ctx) {
    int32_t chan = (int32_t)ctx.gpr[3];
    uint32_t data_array_ptr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];

    int read_count = 0;
    if (chan >= 0 && chan < 4 && data_array_ptr != 0 && length > 0) {
        WiimoteState state = InputManager::get().get_wiimote_state(chan);

        if (state.err == 0) {
            // KPADStatus layout (SDK ~4.x, size 0xF0 / common layout):
            //   0x00: hold (buttons currently pressed)
            //   0x04: trigger (pressed this frame)
            //   0x08: release
            //   0x0C: acc x/y/z (floats) - ignored
            //   0x24: ir (struct) - ignored
            //   0x48: ext_type (uint32, 0=no extension, 1=Nunchuk, 2=Classic)
            //   0x4C: err (int32, 0=KPAD_ERR_NONE)
            //   0x50: classic_hold, etc.
            static uint32_t prev_buttons[4] = {};
            uint32_t hold    = state.buttons;
            uint32_t trigger = hold & ~prev_buttons[chan];  // newly pressed
            uint32_t release = ~hold & prev_buttons[chan];  // newly released
            prev_buttons[chan] = hold;

            ctx.mmu.write32(data_array_ptr + 0x00, hold);
            ctx.mmu.write32(data_array_ptr + 0x04, trigger);
            ctx.mmu.write32(data_array_ptr + 0x08, release);
            // acc
            ctx.mmu.write32(data_array_ptr + 0x0C, 0);
            ctx.mmu.write32(data_array_ptr + 0x10, 0);
            ctx.mmu.write32(data_array_ptr + 0x14, 0);
            // ir - zeroed
            for (int b = 0x18; b < 0x48; b += 4)
                ctx.mmu.write32(data_array_ptr + b, 0);
            // ext_type = 0 (no extension / core Wiimote only)
            ctx.mmu.write32(data_array_ptr + 0x48, 0);
            // err = KPAD_ERR_NONE
            ctx.mmu.write32(data_array_ptr + 0x4C, 0);

            read_count = 1;
        }
    }
    ctx.gpr[3] = read_count;
}

} // extern "C"

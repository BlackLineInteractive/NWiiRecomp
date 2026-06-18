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
    
    // CRITICAL FIX: Do NOT call Raylib Input functions here. 
    // This is executing inside the CPU emulation thread thousands of times a second.
    // Just fetch the cached state updated once per frame by the main thread.
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

    // Disconnected controllers
    for (int i = 1; i < 4; i++) {
        uint32_t offset = pad_status_array_addr + (i * 12);
        ctx.mmu.write16(offset, 0);
        ctx.mmu.write8(offset + 10, (uint8_t)-1); // PAD_ERR_NO_CONTROLLER
    }
}

void WPADRead(CPUContext& ctx) {}

} // extern "C"


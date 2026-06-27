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

void WPADRead(CPUContext& ctx) {
    int32_t chan = (int32_t)ctx.gpr[3];
    uint32_t data_ptr = ctx.gpr[4];
    
    if (chan >= 0 && chan < 4 && data_ptr != 0) {
        WiimoteState state = InputManager::get().get_wiimote_state(chan);
        // Basic WPADStatus structure
        ctx.mmu.write32(data_ptr + 0x00, state.err);
        ctx.mmu.write32(data_ptr + 0x04, state.buttons);
        // In reality, WPADStatus is large, we only fill err and buttons for now
    }
    ctx.gpr[3] = 0; // WPAD_ERR_NONE
}

void KPADRead(CPUContext& ctx) {
    int32_t chan = (int32_t)ctx.gpr[3];
    uint32_t data_array_ptr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5]; // How many structs
    
    int read_count = 0;
    if (chan >= 0 && chan < 4 && data_array_ptr != 0 && length > 0) {
        WiimoteState state = InputManager::get().get_wiimote_state(chan);
        
        // Write one KPADStatus entry
        if (state.err == 0) { // If connected
            ctx.mmu.write32(data_array_ptr + 0x00, state.buttons);       // hold
            ctx.mmu.write32(data_array_ptr + 0x04, 0);                   // trigger (we don't track edges yet)
            ctx.mmu.write32(data_array_ptr + 0x08, 0);                   // release
            
            // IR data (KPADStatus offset 0x24 usually)
            // Simplified: ir.x at 0x28, ir.y at 0x2C
            // We just write some reasonable offsets if KPAD layout is standard
            uint32_t ir_valid_offset = data_array_ptr + 0x24; // depends on SDK version!
            // Actually, we should just write the struct. Let's write the most common fields.
            // But this is highly dependent on KPADStatus structure size which is usually 0x48 or 0x54.
            // We'll write to common offsets used by many games
            
            // For now, we return 1 to indicate 1 struct was read
            read_count = 1;
        }
    }
    ctx.gpr[3] = read_count;
}

} // extern "C"


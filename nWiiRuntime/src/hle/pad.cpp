#include "runtime/cpu_context.h"
#include <iostream>
#include <raylib.h>

using namespace nwii::runtime;

extern "C" {

void PADInit(CPUContext& ctx) {
    std::cout << "[HLE PAD] PADInit: Initializing GameCube Controllers" << std::endl;
}

void PADRead(CPUContext& ctx) {
    uint32_t pad_status_array_addr = ctx.gpr[3];
    
    // We only simulate Controller 0 for now.
    // The libogc PADStatus structure is 12 bytes long:
    // u16 button, s8 stickX, s8 stickY, s8 substickX, s8 substickY, 
    // u8 triggerL, u8 triggerR, u8 analogA, u8 analogB, s8 err, u8 padding
    
    uint16_t buttons = 0;
    if (IsKeyDown(KEY_SPACE)) buttons |= 0x0100; // PAD_BUTTON_A
    if (IsKeyDown(KEY_LEFT_SHIFT)) buttons |= 0x0200; // PAD_BUTTON_B
    if (IsKeyDown(KEY_ENTER)) buttons |= 0x1000; // PAD_BUTTON_START
    if (IsKeyDown(KEY_Q)) buttons |= 0x0400; // PAD_BUTTON_X
    if (IsKeyDown(KEY_E)) buttons |= 0x0800; // PAD_BUTTON_Y
    if (IsKeyDown(KEY_UP)) buttons |= 0x0008; // PAD_BUTTON_UP
    if (IsKeyDown(KEY_DOWN)) buttons |= 0x0004; // PAD_BUTTON_DOWN
    if (IsKeyDown(KEY_LEFT)) buttons |= 0x0001; // PAD_BUTTON_LEFT
    if (IsKeyDown(KEY_RIGHT)) buttons |= 0x0002; // PAD_BUTTON_RIGHT
    
    int8_t stickX = 0;
    int8_t stickY = 0;
    if (IsKeyDown(KEY_D)) stickX = 127;
    if (IsKeyDown(KEY_A)) stickX = -128;
    if (IsKeyDown(KEY_W)) stickY = 127;
    if (IsKeyDown(KEY_S)) stickY = -128;
    
    // Write Controller 0
    ctx.mmu.write16(pad_status_array_addr, buttons);
    ctx.mmu.write8(pad_status_array_addr + 2, (uint8_t)stickX);
    ctx.mmu.write8(pad_status_array_addr + 3, (uint8_t)stickY);
    ctx.mmu.write8(pad_status_array_addr + 4, 0); // substickX
    ctx.mmu.write8(pad_status_array_addr + 5, 0); // substickY
    ctx.mmu.write8(pad_status_array_addr + 6, 0); // trigL
    ctx.mmu.write8(pad_status_array_addr + 7, 0); // trigR
    ctx.mmu.write8(pad_status_array_addr + 8, 0); // analogA
    ctx.mmu.write8(pad_status_array_addr + 9, 0); // analogB
    ctx.mmu.write8(pad_status_array_addr + 10, 0); // err = 0 (OK)
    
    // Mark Controllers 1-3 as disconnected
    for (int i = 1; i < 4; i++) {
        uint32_t offset = pad_status_array_addr + (i * 12);
        ctx.mmu.write16(offset, 0);
        ctx.mmu.write8(offset + 10, -1); // err = PAD_ERR_NO_CONTROLLER (-1)
    }
}

void WPADInit(CPUContext& ctx) {
    std::cout << "[HLE WPAD] WPADInit: Initializing Wii Remotes" << std::endl;
}

void WPADRead(CPUContext& ctx) {
    // uint32_t chan = ctx.gpr[3];
    // uint32_t wpad_data_addr = ctx.gpr[4];
}

void KPADInit(CPUContext& ctx) {
    std::cout << "[HLE KPAD] KPADInit" << std::endl;
}

} // extern "C"

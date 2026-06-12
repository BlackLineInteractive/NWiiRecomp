#include "runtime/cpu_context.h"
#include <iostream>
#include <raylib.h>

using namespace nwii::runtime;


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
    int8_t stickX = 0, stickY = 0;
    int8_t substickX = 0, substickY = 0;
    uint8_t trigL = 0, trigR = 0;
    
    if (IsGamepadAvailable(0)) {
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) buttons |= 0x0100; // PAD_BUTTON_A (Xbox A)
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) buttons |= 0x0200; // PAD_BUTTON_B (Xbox B)
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) buttons |= 0x1000; // PAD_BUTTON_START (Start)
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) buttons |= 0x0400; // PAD_BUTTON_X (Xbox X)
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP)) buttons |= 0x0800; // PAD_BUTTON_Y (Xbox Y)
        
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) buttons |= 0x0008; // PAD_BUTTON_UP
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) buttons |= 0x0004; // PAD_BUTTON_DOWN
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) buttons |= 0x0001; // PAD_BUTTON_LEFT
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) buttons |= 0x0002; // PAD_BUTTON_RIGHT

        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) buttons |= 0x0040; // PAD_TRIGGER_L
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) buttons |= 0x0020; // PAD_TRIGGER_R
        
        stickX = (int8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) * 127.0f);
        stickY = (int8_t)(-GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) * 127.0f); // Y is inverted in GC
        
        substickX = (int8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X) * 127.0f);
        substickY = (int8_t)(-GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y) * 127.0f);
        
        trigL = (uint8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) * 255.0f);
        trigR = (uint8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) * 255.0f);
        
        // Z Button (Right Bumper)
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) buttons |= 0x0010; // PAD_TRIGGER_Z
    } else {
        // Fallback to keyboard if no gamepad
        if (IsKeyDown(KEY_SPACE)) buttons |= 0x0100;
        if (IsKeyDown(KEY_LEFT_SHIFT)) buttons |= 0x0200;
        if (IsKeyDown(KEY_ENTER)) buttons |= 0x1000;
        if (IsKeyDown(KEY_UP)) buttons |= 0x0008;
        if (IsKeyDown(KEY_DOWN)) buttons |= 0x0004;
        if (IsKeyDown(KEY_LEFT)) buttons |= 0x0001;
        if (IsKeyDown(KEY_RIGHT)) buttons |= 0x0002;
        if (IsKeyDown(KEY_W)) stickY = 127;
        if (IsKeyDown(KEY_S)) stickY = -128;
        if (IsKeyDown(KEY_D)) stickX = 127;
        if (IsKeyDown(KEY_A)) stickX = -128;
    }
    
    // Write Controller 0
    ctx.mmu.write16(pad_status_array_addr, buttons);
    ctx.mmu.write8(pad_status_array_addr + 2, (uint8_t)stickX);
    ctx.mmu.write8(pad_status_array_addr + 3, (uint8_t)stickY);
    ctx.mmu.write8(pad_status_array_addr + 4, (uint8_t)substickX);
    ctx.mmu.write8(pad_status_array_addr + 5, (uint8_t)substickY);
    ctx.mmu.write8(pad_status_array_addr + 6, trigL);
    ctx.mmu.write8(pad_status_array_addr + 7, trigR);
    ctx.mmu.write8(pad_status_array_addr + 8, 0); // err = 0 (OK)
    // Write 0 to the rest of the padding/analog fields
    ctx.mmu.write8(pad_status_array_addr + 9, 0);
    ctx.mmu.write8(pad_status_array_addr + 10, 0);
    ctx.mmu.write8(pad_status_array_addr + 11, 0);
    
    // Mark Controllers 1-3 as disconnected
    for (int i = 1; i < 4; i++) {
        uint32_t offset = pad_status_array_addr + (i * 12);
        ctx.mmu.write16(offset, 0);
        ctx.mmu.write8(offset + 8, (uint8_t)-1); // err = PAD_ERR_NO_CONTROLLER (-1)
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


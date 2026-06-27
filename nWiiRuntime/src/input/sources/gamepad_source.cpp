#include "gamepad_source.h"
#include <raylib.h>

namespace nwii::runtime::input {

void GamepadSource::update(GameCubePadState pads[4], WiimoteState motes[4]) {
    for (int i = 0; i < 4; ++i) {
        if (IsGamepadAvailable(i)) {
            pads[i].err = 0; // Connected
            motes[i].err = 0; // Connected

            // Wiimote mapping
            float rx = GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_X);
            float ry = GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_Y);
            
            // Simple integration of stick velocity into IR position
            motes[i].ir_x += rx * 0.05f;
            motes[i].ir_y += ry * 0.05f;
            
            if (motes[i].ir_x < 0.0f) motes[i].ir_x = 0.0f;
            if (motes[i].ir_x > 1.0f) motes[i].ir_x = 1.0f;
            if (motes[i].ir_y < 0.0f) motes[i].ir_y = 0.0f;
            if (motes[i].ir_y > 1.0f) motes[i].ir_y = 1.0f;

            // Map WPAD buttons
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) motes[i].buttons |= 0x0800; // WPAD_BUTTON_A
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) motes[i].buttons |= 0x0400; // WPAD_BUTTON_B
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_MIDDLE_RIGHT)) motes[i].buttons |= 0x0010; // WPAD_BUTTON_PLUS
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_MIDDLE_LEFT)) motes[i].buttons |= 0x0001; // WPAD_BUTTON_MINUS

            // GC Pad mapping
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) pads[i].buttons |= 0x0100;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) pads[i].buttons |= 0x0200;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_MIDDLE_RIGHT)) pads[i].buttons |= 0x1000;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) pads[i].buttons |= 0x0400;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_UP)) pads[i].buttons |= 0x0800;
            
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_UP)) pads[i].buttons |= 0x0008;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) pads[i].buttons |= 0x0004;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) pads[i].buttons |= 0x0001;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) pads[i].buttons |= 0x0002;

            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) pads[i].buttons |= 0x0040;
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) pads[i].buttons |= 0x0020;
            
            pads[i].stick_x = (int8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_X) * 127.0f);
            pads[i].stick_y = (int8_t)(-GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_Y) * 127.0f);
            pads[i].substick_x = (int8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_X) * 127.0f);
            pads[i].substick_y = (int8_t)(-GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_Y) * 127.0f);
            pads[i].trigger_l = (uint8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_TRIGGER) * 255.0f);
            pads[i].trigger_r = (uint8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_TRIGGER) * 255.0f);
            
            if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) pads[i].buttons |= 0x0010;
        }
    }
}

} // namespace nwii::runtime::input

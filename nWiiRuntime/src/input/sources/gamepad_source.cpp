#include "gamepad_source.h"
#include "runtime/config.h"
#include <raylib.h>

namespace nwii::runtime::input {

// Modes (config [input] mode):
//   2 = plain gamepad: buttons/sticks only, no pointer emulation
//   3 = gamepad + pointer assist: right stick (or pad gyro when the
//       driver exposes it as extra axes) nudges the IR pointer
//   4 = full tilt: gyro/extra axes steer the pointer directly with
//       gyro_sensitivity; falls back to right stick if no gyro
void GamepadSource::update(GameCubePadState pads[4], WiimoteState motes[4]) {
    auto& cfg = nwii::runtime::Config::get();
    int mode = cfg.input_mode;
    bool want_pointer = (mode == 3 || mode == 4);

    for (int i = 0; i < 4; ++i) {
        if (!IsGamepadAvailable(i))
            continue;
        pads[i].err = 0;  // connected
        motes[i].err = 0; // connected

        // Pointer emulation from stick or gyro
        if (want_pointer) {
            float dx = 0.0f, dy = 0.0f;
            int axes = GetGamepadAxisCount(i);
            // Some drivers expose gyro/accelerometer as axes 6+; use them
            // when present, otherwise the right stick.
            if (axes > 6) {
                dx = GetGamepadAxisMovement(i, 6) * cfg.gyro_sensitivity;
                dy = GetGamepadAxisMovement(i, 7) * cfg.gyro_sensitivity;
            } else {
                dx = GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_X);
                dy = GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_Y);
            }
            float speed = (mode == 4)
                ? 0.10f * cfg.gyro_sensitivity
                : 0.05f * cfg.pointer_speed;
            motes[i].ir_x += dx * speed;
            motes[i].ir_y += dy * speed;
            if (motes[i].ir_x < 0.0f) motes[i].ir_x = 0.0f;
            if (motes[i].ir_x > 1.0f) motes[i].ir_x = 1.0f;
            if (motes[i].ir_y < 0.0f) motes[i].ir_y = 0.0f;
            if (motes[i].ir_y > 1.0f) motes[i].ir_y = 1.0f;

            // Rough accelerometer synthesis so games that read tilt get
            // something consistent with the pointer motion
            motes[i].accel_x = dx;
            motes[i].accel_y = dy;
            motes[i].accel_z = 1.0f;
        }

        // WPAD buttons (all gamepad modes)
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) motes[i].buttons |= 0x0800; // A
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) motes[i].buttons |= 0x0400; // B
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) motes[i].buttons |= 0x0002; // 1
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_UP)) motes[i].buttons |= 0x0001;   // 2
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_MIDDLE_RIGHT)) motes[i].buttons |= 0x0010; // Plus
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_MIDDLE_LEFT)) motes[i].buttons |= 0x1000;  // Minus
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_MIDDLE)) motes[i].buttons |= 0x0080;       // Home
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_UP)) motes[i].buttons |= 0x0008;
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) motes[i].buttons |= 0x0004;
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) motes[i].buttons |= 0x0001;
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) motes[i].buttons |= 0x0002;

        // GC pad / Classic Controller mapping (all gamepad modes)
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) pads[i].buttons |= 0x0100; // A
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) pads[i].buttons |= 0x0200; // B
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_MIDDLE_RIGHT)) pads[i].buttons |= 0x1000;  // Start
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) pads[i].buttons |= 0x0400; // X
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_FACE_UP)) pads[i].buttons |= 0x0800; // Y
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_UP)) pads[i].buttons |= 0x0008;
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) pads[i].buttons |= 0x0004;
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) pads[i].buttons |= 0x0001;
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) pads[i].buttons |= 0x0002;
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) pads[i].buttons |= 0x0040;  // L
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) pads[i].buttons |= 0x0020; // R
        if (IsGamepadButtonDown(i, GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) pads[i].buttons |= 0x0010; // Z

        pads[i].stick_x = (int8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_X) * 127.0f);
        pads[i].stick_y = (int8_t)(-GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_Y) * 127.0f);
        // In pointer modes the right stick belongs to the pointer
        if (!want_pointer) {
            pads[i].substick_x = (int8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_X) * 127.0f);
            pads[i].substick_y = (int8_t)(-GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_Y) * 127.0f);
        }
        pads[i].trigger_l = (uint8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_LEFT_TRIGGER) * 255.0f);
        pads[i].trigger_r = (uint8_t)(GetGamepadAxisMovement(i, GAMEPAD_AXIS_RIGHT_TRIGGER) * 255.0f);
    }
}

} // namespace nwii::runtime::input

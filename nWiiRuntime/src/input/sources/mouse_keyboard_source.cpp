#include "mouse_keyboard_source.h"
#include <raylib.h>

namespace nwii::runtime::input {

void MouseKeyboardSource::update(GameCubePadState pads[4], WiimoteState motes[4]) {
    // Only update controller 0 for now
    Vector2 mouse_pos = GetMousePosition();
    motes[0].err = 0; // mark as connected
    motes[0].ir_x = mouse_pos.x / (float)GetScreenWidth();
    motes[0].ir_y = mouse_pos.y / (float)GetScreenHeight();

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) motes[0].buttons |= 0x0800; // WPAD_BUTTON_A
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) motes[0].buttons |= 0x0400; // WPAD_BUTTON_B
    if (IsKeyDown(KEY_ENTER)) motes[0].buttons |= 0x0010; // WPAD_BUTTON_PLUS
    if (IsKeyDown(KEY_BACKSPACE)) motes[0].buttons |= 0x0001; // WPAD_BUTTON_MINUS
    if (IsKeyDown(KEY_UP)) motes[0].buttons |= 0x0008; // WPAD_BUTTON_UP
    if (IsKeyDown(KEY_DOWN)) motes[0].buttons |= 0x0004; // WPAD_BUTTON_DOWN
    if (IsKeyDown(KEY_LEFT)) motes[0].buttons |= 0x0001; // WPAD_BUTTON_LEFT
    if (IsKeyDown(KEY_RIGHT)) motes[0].buttons |= 0x0002; // WPAD_BUTTON_RIGHT

    pads[0].err = 0; // mark as connected
    if (IsKeyDown(KEY_SPACE)) pads[0].buttons |= 0x0100;
    if (IsKeyDown(KEY_ENTER)) pads[0].buttons |= 0x1000;
    if (IsKeyDown(KEY_UP)) pads[0].buttons |= 0x0008;
    if (IsKeyDown(KEY_DOWN)) pads[0].buttons |= 0x0004;
    if (IsKeyDown(KEY_LEFT)) pads[0].buttons |= 0x0001;
    if (IsKeyDown(KEY_RIGHT)) pads[0].buttons |= 0x0002;
    if (IsKeyDown(KEY_W)) pads[0].stick_y = 127;
    if (IsKeyDown(KEY_S)) pads[0].stick_y = -128;
    if (IsKeyDown(KEY_D)) pads[0].stick_x = 127;
    if (IsKeyDown(KEY_A)) pads[0].stick_x = -128;
}

} // namespace nwii::runtime::input

#include "input/input_manager.h"
#include <raylib.h>
#include <iostream>

namespace nwii::runtime::input {

InputManager& InputManager::get() {
    static InputManager instance;
    return instance;
}

InputManager::InputManager() {
    for (int i = 0; i < 4; ++i) {
        gc_pads[i] = {0, 0, 0, 0, 0, 0, 0, 0, 0, -1};
        wii_motes[i] = {0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1};
    }
    // Controller 0 is connected
    gc_pads[0].err = 0;
    wii_motes[0].err = 0;
}

void InputManager::set_mode(InputMode mode) {
    current_mode = mode;
    std::cout << "[InputManager] Switched input mode to " << (int)mode << std::endl;
}

void InputManager::update() {
    // Check for hotkeys to swap modes
    if (IsKeyPressed(KEY_F1)) set_mode(InputMode::GamepadClassic);
    if (IsKeyPressed(KEY_F2)) set_mode(InputMode::GamepadWiimote);
    if (IsKeyPressed(KEY_F3)) set_mode(InputMode::MouseKeyboard);
    if (IsKeyPressed(KEY_F4)) set_mode(InputMode::Remote);

    // Reset state before polling
    gc_pads[0].buttons = 0;
    wii_motes[0].buttons = 0;
    gc_pads[0].stick_x = 0;
    gc_pads[0].stick_y = 0;
    gc_pads[0].substick_x = 0;
    gc_pads[0].substick_y = 0;

    switch (current_mode) {
        case InputMode::GamepadClassic: update_gamepad_classic(); break;
        case InputMode::GamepadWiimote: update_gamepad_wiimote(); break;
        case InputMode::MouseKeyboard:  update_mouse_keyboard(); break;
        case InputMode::Touch:          update_touch(); break;
        case InputMode::Remote:         update_remote(); break;
    }
}

void InputManager::update_gamepad_classic() {
    if (IsGamepadAvailable(0)) {
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) gc_pads[0].buttons |= 0x0100;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) gc_pads[0].buttons |= 0x0200;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) gc_pads[0].buttons |= 0x1000;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) gc_pads[0].buttons |= 0x0400;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP)) gc_pads[0].buttons |= 0x0800;
        
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) gc_pads[0].buttons |= 0x0008;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) gc_pads[0].buttons |= 0x0004;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) gc_pads[0].buttons |= 0x0001;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) gc_pads[0].buttons |= 0x0002;

        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) gc_pads[0].buttons |= 0x0040;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) gc_pads[0].buttons |= 0x0020;
        
        gc_pads[0].stick_x = (int8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) * 127.0f);
        gc_pads[0].stick_y = (int8_t)(-GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) * 127.0f);
        gc_pads[0].substick_x = (int8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X) * 127.0f);
        gc_pads[0].substick_y = (int8_t)(-GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y) * 127.0f);
        gc_pads[0].trigger_l = (uint8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) * 255.0f);
        gc_pads[0].trigger_r = (uint8_t)(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) * 255.0f);
        
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) gc_pads[0].buttons |= 0x0010;
    }
}

void InputManager::update_gamepad_wiimote() {
    if (IsGamepadAvailable(0)) {
        float rx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
        float ry = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
        
        // Simple integration of stick velocity into IR position
        wii_motes[0].ir_x += rx * 0.05f;
        wii_motes[0].ir_y += ry * 0.05f;
        
        if (wii_motes[0].ir_x < 0.0f) wii_motes[0].ir_x = 0.0f;
        if (wii_motes[0].ir_x > 1.0f) wii_motes[0].ir_x = 1.0f;
        if (wii_motes[0].ir_y < 0.0f) wii_motes[0].ir_y = 0.0f;
        if (wii_motes[0].ir_y > 1.0f) wii_motes[0].ir_y = 1.0f;

        // Map WPAD buttons
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) wii_motes[0].buttons |= 0x0800; // WPAD_BUTTON_A
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) wii_motes[0].buttons |= 0x0400; // WPAD_BUTTON_B
    }
}

void InputManager::update_mouse_keyboard() {
    Vector2 mouse_pos = GetMousePosition();
    wii_motes[0].ir_x = mouse_pos.x / (float)GetScreenWidth();
    wii_motes[0].ir_y = mouse_pos.y / (float)GetScreenHeight();

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) wii_motes[0].buttons |= 0x0800; // WPAD_BUTTON_A
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) wii_motes[0].buttons |= 0x0400; // WPAD_BUTTON_B
    if (IsKeyDown(KEY_ENTER)) wii_motes[0].buttons |= 0x0010; // WPAD_BUTTON_PLUS

    if (IsKeyDown(KEY_SPACE)) gc_pads[0].buttons |= 0x0100;
    if (IsKeyDown(KEY_ENTER)) gc_pads[0].buttons |= 0x1000;
    if (IsKeyDown(KEY_UP)) gc_pads[0].buttons |= 0x0008;
    if (IsKeyDown(KEY_DOWN)) gc_pads[0].buttons |= 0x0004;
    if (IsKeyDown(KEY_LEFT)) gc_pads[0].buttons |= 0x0001;
    if (IsKeyDown(KEY_RIGHT)) gc_pads[0].buttons |= 0x0002;
    if (IsKeyDown(KEY_W)) gc_pads[0].stick_y = 127;
    if (IsKeyDown(KEY_S)) gc_pads[0].stick_y = -128;
    if (IsKeyDown(KEY_D)) gc_pads[0].stick_x = 127;
    if (IsKeyDown(KEY_A)) gc_pads[0].stick_x = -128;
}

void InputManager::update_touch() {
    update_mouse_keyboard();
}

void InputManager::update_remote() {
    update_mouse_keyboard();
}

GameCubePadState InputManager::get_gcpad_state(int index) {
    if (index >= 0 && index < 4) return gc_pads[index];
    return {0,0,0,0,0,0,0,0,0,-1};
}

WiimoteState InputManager::get_wiimote_state(int index) {
    if (index >= 0 && index < 4) return wii_motes[index];
    return {0,0.5f,0.5f,0,0,0,0,0,0,-1};
}

} // namespace nwii::runtime::input

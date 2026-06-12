#pragma once
#include <cstdint>
#include <vector>

namespace nwii::runtime::input {

enum class InputMode {
    GamepadClassic, // Gamepad acting as GC/Classic controller
    GamepadWiimote, // Gamepad + Right Stick for IR
    MouseKeyboard,  // Mouse & Keyboard for IR and buttons
    Touch,          // Touchscreen
    Remote          // Smartphone remote
};

struct GameCubePadState {
    uint16_t buttons;
    int8_t stick_x, stick_y;
    int8_t substick_x, substick_y;
    uint8_t trigger_l, trigger_r;
    uint8_t analog_a, analog_b;
    int8_t err;
};

struct WiimoteState {
    uint32_t buttons;
    float ir_x, ir_y; // 0.0 to 1.0
    float accel_x, accel_y, accel_z;
    float gyro_pitch, gyro_yaw, gyro_roll;
    int8_t err;
};

class InputManager {
public:
    static InputManager& get();

    void update();
    void set_mode(InputMode mode);
    InputMode get_mode() const { return current_mode; }

    GameCubePadState get_gcpad_state(int index);
    WiimoteState get_wiimote_state(int index);

private:
    InputManager();
    ~InputManager() = default;

    InputMode current_mode = InputMode::GamepadClassic;

    GameCubePadState gc_pads[4];
    WiimoteState wii_motes[4];

    void update_gamepad_classic();
    void update_gamepad_wiimote();
    void update_mouse_keyboard();
    void update_touch();
    void update_remote();
};

} // namespace nwii::runtime::input

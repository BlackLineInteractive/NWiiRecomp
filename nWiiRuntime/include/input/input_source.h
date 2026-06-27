#pragma once
#include <cstdint>

namespace nwii::runtime::input {

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

class IInputSource {
public:
    virtual ~IInputSource() = default;

    // Called once per frame. The source should update the relevant arrays.
    // If multiple sources write to the same index, they should combine inputs 
    // (e.g. by doing pads[i].buttons |= my_buttons).
    virtual void update(GameCubePadState pads[4], WiimoteState motes[4]) = 0;
};

} // namespace nwii::runtime::input

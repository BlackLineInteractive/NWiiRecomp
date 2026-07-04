#pragma once
#include "input/input_source.h"

namespace nwii::runtime::input {

// Smartphone as Wiimote (input mode 5). Listens on a UDP port (config
// [input] phone_port, default 4313) for newline-free ASCII datagrams:
//
//   BTN <hex mask>        WPAD button mask, e.g. "BTN 800" = A held
//   IR <x> <y>            pointer position, both 0.0-1.0
//   ACC <x> <y> <z>       accelerometer in g units
//   GYR <pitch> <yaw> <roll>  angular rate, applied to the pointer with
//                             gyro_sensitivity like a real Wiimote
//
// Any app or script that can send UDP works as a controller; several
// generic "sensor streamer" phone apps can be pointed at this port.
class PhoneSource : public IInputSource {
public:
    PhoneSource();
    ~PhoneSource() override;
    void update(GameCubePadState pads[4], WiimoteState motes[4]) override;

private:
    int m_socket = -1;
    uint32_t m_buttons = 0;
    float m_ir_x = 0.5f, m_ir_y = 0.5f;
    float m_accel_x = 0.0f, m_accel_y = 0.0f, m_accel_z = 1.0f;
};

} // namespace nwii::runtime::input

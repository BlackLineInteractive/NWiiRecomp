#pragma once
#include "input/input_source.h"

namespace nwii::runtime::input {








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

} 

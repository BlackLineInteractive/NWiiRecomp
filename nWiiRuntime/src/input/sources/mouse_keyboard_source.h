#pragma once
#include "input/input_source.h"

namespace nwii::runtime::input {

class MouseKeyboardSource : public IInputSource {
public:
    MouseKeyboardSource() = default;
    ~MouseKeyboardSource() override = default;

    void update(GameCubePadState pads[4], WiimoteState motes[4]) override;
};

} 

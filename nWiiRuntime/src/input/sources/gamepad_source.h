#pragma once
#include "input/input_source.h"

namespace nwii::runtime::input {

class GamepadSource : public IInputSource {
public:
    GamepadSource() = default;
    ~GamepadSource() override = default;

    void update(GameCubePadState pads[4], WiimoteState motes[4]) override;
};

} // namespace nwii::runtime::input

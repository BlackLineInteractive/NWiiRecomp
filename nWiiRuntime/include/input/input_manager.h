#pragma once
#include "input/input_source.h"
#include <cstdint>
#include <vector>
#include <memory>

namespace nwii::runtime::input {

enum class InputMode {
    GamepadClassic, // Gamepad acting as GC/Classic controller
    GamepadWiimote, // Gamepad + Right Stick for IR
    MouseKeyboard,  // Mouse & Keyboard for IR and buttons
    Touch,          // Touchscreen
    Remote          // Smartphone remote
};

class InputManager {
public:
    static InputManager& get();

    void update();
    void set_mode(InputMode mode);
    InputMode get_mode() const { return current_mode; }

    void add_source(std::unique_ptr<IInputSource> source);

    GameCubePadState get_gcpad_state(int index);
    WiimoteState get_wiimote_state(int index);

private:
    InputManager();
    ~InputManager() = default;

    InputMode current_mode = InputMode::GamepadClassic;

    GameCubePadState gc_pads[4];
    WiimoteState wii_motes[4];

    std::vector<std::unique_ptr<IInputSource>> sources;
};

} // namespace nwii::runtime::input


#include "input/input_manager.h"
#include <iostream>
#include <algorithm>

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
}

void InputManager::add_source(std::unique_ptr<IInputSource> source) {
    if (source) {
        sources.push_back(std::move(source));
        std::cout << "[InputManager] Registered a new input source. Total sources: " << sources.size() << std::endl;
    }
}

void InputManager::set_mode(InputMode mode) {
    current_mode = mode;
    std::cout << "[InputManager] Switched input mode to " << (int)mode << std::endl;
}

void InputManager::update() {

    

    for (int i = 0; i < 4; ++i) {
        gc_pads[i].buttons = 0;
        gc_pads[i].stick_x = 0;
        gc_pads[i].stick_y = 0;
        gc_pads[i].substick_x = 0;
        gc_pads[i].substick_y = 0;
        gc_pads[i].trigger_l = 0;
        gc_pads[i].trigger_r = 0;
        gc_pads[i].analog_a = 0;
        gc_pads[i].analog_b = 0;
        gc_pads[i].err = -1; 

        wii_motes[i].buttons = 0;
        wii_motes[i].err = -1; 
    }

    for (auto& source : sources) {
        source->update(gc_pads, wii_motes);
    }
}

GameCubePadState InputManager::get_gcpad_state(int index) {
    if (index >= 0 && index < 4) return gc_pads[index];
    return {0,0,0,0,0,0,0,0,0,-1};
}

WiimoteState InputManager::get_wiimote_state(int index) {
    if (index >= 0 && index < 4) return wii_motes[index];
    return {0,0.5f,0.5f,0,0,0,0,0,0,-1};
}

} 

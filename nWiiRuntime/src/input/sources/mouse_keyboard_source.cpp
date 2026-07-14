#include "mouse_keyboard_source.h"
#include "runtime/config.h"
#include <SDL.h>

namespace nwii::runtime::input {

void MouseKeyboardSource::update(GameCubePadState pads[4], WiimoteState motes[4]) {
    int mx, my;
    uint32_t mouse_state = SDL_GetMouseState(&mx, &my);
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    motes[0].err = 0; // mark as connected
    int w = nwii::runtime::Config::get().window_width;
    int h = nwii::runtime::Config::get().window_height;
    if (w == 0) w = 640;
    if (h == 0) h = 480;
    
    motes[0].ir_x = (float)mx / (float)w;
    motes[0].ir_y = (float)my / (float)h;

    if (mouse_state & SDL_BUTTON(SDL_BUTTON_LEFT)) motes[0].buttons |= 0x0800; // WPAD_BUTTON_A
    if (mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT)) motes[0].buttons |= 0x0400; // WPAD_BUTTON_B
    if (keys[SDL_SCANCODE_RETURN]) motes[0].buttons |= 0x0010; // WPAD_BUTTON_PLUS
    if (keys[SDL_SCANCODE_BACKSPACE]) motes[0].buttons |= 0x0001; // WPAD_BUTTON_MINUS
    if (keys[SDL_SCANCODE_UP]) motes[0].buttons |= 0x0008; // WPAD_BUTTON_UP
    if (keys[SDL_SCANCODE_DOWN]) motes[0].buttons |= 0x0004; // WPAD_BUTTON_DOWN
    if (keys[SDL_SCANCODE_LEFT]) motes[0].buttons |= 0x0001; // WPAD_BUTTON_LEFT
    if (keys[SDL_SCANCODE_RIGHT]) motes[0].buttons |= 0x0002; // WPAD_BUTTON_RIGHT

    pads[0].err = 0; // mark as connected
    if (keys[SDL_SCANCODE_SPACE]) pads[0].buttons |= 0x0100;
    if (keys[SDL_SCANCODE_RETURN]) pads[0].buttons |= 0x1000;
    if (keys[SDL_SCANCODE_UP]) pads[0].buttons |= 0x0008;
    if (keys[SDL_SCANCODE_DOWN]) pads[0].buttons |= 0x0004;
    if (keys[SDL_SCANCODE_LEFT]) pads[0].buttons |= 0x0001;
    if (keys[SDL_SCANCODE_RIGHT]) pads[0].buttons |= 0x0002;
    if (keys[SDL_SCANCODE_W]) pads[0].stick_y = 127;
    if (keys[SDL_SCANCODE_S]) pads[0].stick_y = -128;
    if (keys[SDL_SCANCODE_D]) pads[0].stick_x = 127;
    if (keys[SDL_SCANCODE_A]) pads[0].stick_x = -128;
}

} // namespace nwii::runtime::input

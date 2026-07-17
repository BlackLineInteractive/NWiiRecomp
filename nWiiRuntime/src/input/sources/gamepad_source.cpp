#include "gamepad_source.h"
#include "runtime/config.h"
#include <SDL.h>
#include <cmath>

namespace nwii::runtime::input {

void GamepadSource::update(GameCubePadState pads[4], WiimoteState motes[4]) {
    auto& cfg = nwii::runtime::Config::get();
    int mode = cfg.input_mode;
    bool want_pointer = (mode == 3 || mode == 4);

    for (int i = 0; i < 4; ++i) {
        if (!SDL_IsGameController(i))
            continue;
            
        SDL_GameController* controller = SDL_GameControllerOpen(i);
        if (!controller) continue;

        pads[i].err = 0;  
        motes[i].err = 0; 

        if (want_pointer) {
            float dx = 0.0f, dy = 0.0f;
            
            if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO)) {
                SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE);
                float data[3];
                if (SDL_GameControllerGetSensorData(controller, SDL_SENSOR_GYRO, data, 3) == 0) {
                    dx = data[0] * cfg.gyro_sensitivity;
                    dy = data[1] * cfg.gyro_sensitivity;
                }
            } else {
                int rx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
                int ry = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
                
                if (std::abs(rx) > 8000) dx = (rx / 32767.0f);
                if (std::abs(ry) > 8000) dy = (ry / 32767.0f);
            }
            float speed = (mode == 4)
                ? 0.10f * cfg.gyro_sensitivity
                : 0.05f * cfg.pointer_speed;
            motes[i].ir_x += dx * speed;
            motes[i].ir_y += dy * speed;
            if (motes[i].ir_x < 0.0f) motes[i].ir_x = 0.0f;
            if (motes[i].ir_x > 1.0f) motes[i].ir_x = 1.0f;
            if (motes[i].ir_y < 0.0f) motes[i].ir_y = 0.0f;
            if (motes[i].ir_y > 1.0f) motes[i].ir_y = 1.0f;

            motes[i].accel_x = dx;
            motes[i].accel_y = dy;
            motes[i].accel_z = 1.0f;
        }

        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) motes[i].buttons |= 0x0800; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B)) motes[i].buttons |= 0x0400; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X)) motes[i].buttons |= 0x0002; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y)) motes[i].buttons |= 0x0001; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) motes[i].buttons |= 0x0010; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK)) motes[i].buttons |= 0x1000;  
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_GUIDE)) motes[i].buttons |= 0x0080; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) motes[i].buttons |= 0x0008;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) motes[i].buttons |= 0x0004;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) motes[i].buttons |= 0x0001;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) motes[i].buttons |= 0x0002;

        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) pads[i].buttons |= 0x0100; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B)) pads[i].buttons |= 0x0200; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) pads[i].buttons |= 0x1000;  
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X)) pads[i].buttons |= 0x0400; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y)) pads[i].buttons |= 0x0800; 
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) pads[i].buttons |= 0x0008;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) pads[i].buttons |= 0x0004;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) pads[i].buttons |= 0x0001;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) pads[i].buttons |= 0x0002;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) pads[i].buttons |= 0x0040;  
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) pads[i].buttons |= 0x0020; 
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384) pads[i].buttons |= 0x0010; 

        int lx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        if (std::abs(lx) > 8000) pads[i].stick_x = (int8_t)((lx / 32767.0f) * 127.0f);
        if (std::abs(ly) > 8000) pads[i].stick_y = (int8_t)((-ly / 32767.0f) * 127.0f); 
        
        if (!want_pointer) {
            int rx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
            int ry = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
            if (std::abs(rx) > 8000) pads[i].substick_x = (int8_t)((rx / 32767.0f) * 127.0f);
            if (std::abs(ry) > 8000) pads[i].substick_y = (int8_t)((-ry / 32767.0f) * 127.0f);
        }
        
        pads[i].trigger_l = (uint8_t)((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f) * 255.0f);
        pads[i].trigger_r = (uint8_t)((SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f) * 255.0f);
    }
}

} 

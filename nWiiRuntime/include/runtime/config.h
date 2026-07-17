#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace nwii {
namespace runtime {

enum class Platform {
    Wii,
    GameCube,
    WiiU
};

enum class Backend {
    OpenGL,
    Metal
};

struct GameProfile {

    uint32_t callback_stack_top = 0;

    std::vector<std::string> disable_modules;

    
    std::string rpl_path;

    

    uint32_t ext_interrupt_dispatch = 0;

    

    uint32_t sched_lock_offset = 0;
};

class Config {
public:
    static Config& get() {
        static Config instance;
        return instance;
    }

    void load(const std::string& filepath);

    
    void load_game_profile();

    Platform platform = Platform::Wii;
    Backend backend = Backend::OpenGL;
    bool enable_vsync = true;
    int window_width = 640;
    int window_height = 480;
    std::string game_dir;

    
    
    std::string game_id = "RSZK";

    

    

    
    int input_mode = 6;

    float gyro_sensitivity = 1.0f;

    float pointer_speed = 1.0f;

    int phone_port = 4313;

    GameProfile game_profile;

private:
    Config() = default;
};

} 
} 

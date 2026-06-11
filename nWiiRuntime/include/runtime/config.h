#pragma once
#include <string>

namespace nwii {
namespace runtime {

enum class Platform {
    Wii,
    GameCube
};

class Config {
public:
    static Config& get() {
        static Config instance;
        return instance;
    }

    void load(const std::string& filepath);

    Platform platform = Platform::Wii;
    bool enable_vsync = true;
    int window_width = 640;
    int window_height = 480;
    std::string game_dir;
    // 4-char game ID (e.g. "RSZK" for Silent Hill Shattered Memories).
    // Used in DI_ReadDiskID. Override via config.toml: game_id = "RSZK"
    std::string game_id = "RSZK";

private:
    Config() = default;
};

} // namespace runtime
} // namespace nwii

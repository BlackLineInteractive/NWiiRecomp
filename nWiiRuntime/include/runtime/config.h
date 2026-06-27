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

// Per-game quirks loaded from assets/games/<game_id>.toml
struct GameProfile {
    // Override callback stack top if the game uses 0x816F0000 for its own data.
    // 0 = use default (0x816F0000)
    uint32_t callback_stack_top = 0;

    // Modules to disable for this game (e.g. "usb" if BT causes hangs)
    std::vector<std::string> disable_modules;

    // Path to RPX file (Wii U only, relative to game_dir)
    // e.g. "code/game.rpx"
    std::string rpl_path;
};

class Config {
public:
    static Config& get() {
        static Config instance;
        return instance;
    }

    void load(const std::string& filepath);

    // Loads per-game profile from assets/games/<game_id>.toml
    // Call this after game_id is known (after DOL/RPX header is parsed).
    void load_game_profile();

    Platform platform = Platform::Wii;
    bool enable_vsync = true;
    int window_width = 640;
    int window_height = 480;
    std::string game_dir;

    // 4-char game ID (e.g. "RSZK" for Silent Hill Shattered Memories).
    // Read from DOL disc header / RPX TitleID.
    // Can be overridden via config.toml: game_id = "RSZK"
    std::string game_id = "RSZK";

    // Gyro input sensitivity (0.1 = very subtle, 2.0 = very responsive)
    float gyro_sensitivity = 1.0f;

    // Per-game quirks (loaded from assets/games/<game_id>.toml after DOL/RPX parse)
    GameProfile game_profile;

private:
    Config() = default;
};

} // namespace runtime
} // namespace nwii

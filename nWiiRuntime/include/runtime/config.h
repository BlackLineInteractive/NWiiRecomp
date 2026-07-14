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

    // Address of guest __OSDispatchInterrupt for post-ISR reschedule.
    // 0 = auto-detect from ExcTable[4] ([0x80003000+16]) at runtime.
    // Set explicitly in [hle] ext_interrupt_dispatch = 0x... for games
    // where ExcTable[4] is zero (e.g. MP7 which uses physical 0x80000500).
    uint32_t ext_interrupt_dispatch = 0;

    // Offset from r13 (SDA base) to the OS scheduler disable-count word
    // (§6.3 guard: don't interrupt __OSReschedule mid-critical-section).
    // 0 = unknown for this game -> SKIP the guard rather than risk a false
    // positive that starves every external interrupt. MP7 (GP7P01): 0x6e60.
    uint32_t sched_lock_offset = 0;
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
    Backend backend = Backend::OpenGL;
    bool enable_vsync = true;
    int window_width = 640;
    int window_height = 480;
    std::string game_dir;

    // 4-char game ID (e.g. "RSZK" for Silent Hill Shattered Memories).
    // Read from DOL disc header / RPX TitleID.
    // Can be overridden via config.toml: game_id = "RSZK"
    std::string game_id = "RSZK";

    // Input mode, selectable in config.toml [input] mode = N:
    //   1 real Wiimote over Bluetooth        (planned)
    //   2 gamepad as Classic Controller / GC pad
    //   3 gamepad + motion-assisted pointer (right stick or pad gyro)
    //   4 gamepad with full tilt control and sensitivity setting
    //   5 smartphone as Wiimote/pointer over UDP
    //   6 keyboard + mouse (mouse = IR pointer)
    //   7 touch screen                        (planned)
    int input_mode = 6;

    // Gyro/tilt input sensitivity (0.1 = very subtle, 2.0 = very responsive)
    float gyro_sensitivity = 1.0f;

    // Pointer speed for stick-driven IR movement (mode 3)
    float pointer_speed = 1.0f;

    // UDP port for the smartphone input server (mode 5)
    int phone_port = 4313;

    // Per-game quirks (loaded from assets/games/<game_id>.toml after DOL/RPX parse)
    GameProfile game_profile;

private:
    Config() = default;
};

} // namespace runtime
} // namespace nwii

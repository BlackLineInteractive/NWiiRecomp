#include "runtime/config.h"
#include <toml++/toml.hpp>
#include <iostream>
#include <filesystem>

namespace nwii {
namespace runtime {

void Config::load(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        std::cout << "[Config] Config file " << filepath << " not found, using defaults." << std::endl;
        return;
    }

    try {
        toml::table tbl = toml::parse_file(filepath);

        auto plat = tbl["system"]["platform"].value<std::string>();
        if (plat) {
            if (*plat == "GameCube" || *plat == "gamecube") {
                platform = Platform::GameCube;
                std::cout << "[Config] Platform set to GameCube." << std::endl;
            } else if (*plat == "WiiU" || *plat == "wiiu" || *plat == "Wii U") {
                platform = Platform::WiiU;
                std::cout << "[Config] Platform set to Wii U." << std::endl;
            } else {
                platform = Platform::Wii;
                std::cout << "[Config] Platform set to Wii." << std::endl;
            }
        }

        auto vsync = tbl["graphics"]["vsync"].value<bool>();
        if (vsync) enable_vsync = *vsync;

        auto w = tbl["graphics"]["window_width"].value<int>();
        if (w) window_width = *w;

        auto h = tbl["graphics"]["window_height"].value<int>();
        if (h) window_height = *h;

        auto gid = tbl["game"]["id"].value<std::string>();
        if (gid) {
            game_id = *gid;
            std::cout << "[Config] Game ID override: " << game_id << std::endl;
        }

        auto mode = tbl["input"]["mode"].value<int>();
        if (mode) {
            input_mode = *mode;
            static const char* mode_names[] = {
                "", "wiimote-bluetooth", "gamepad-classic",
                "gamepad-pointer-assist", "gamepad-full-tilt",
                "smartphone", "keyboard-mouse", "touch"
            };
            if (input_mode >= 1 && input_mode <= 7)
                std::cout << "[Config] Input mode " << input_mode << " ("
                          << mode_names[input_mode] << ")" << std::endl;
            else
                std::cout << "[Config] Unknown input mode " << input_mode
                          << ", falling back to 6 (keyboard-mouse)" << std::endl;
        }

        auto gyro = tbl["input"]["gyro_sensitivity"].value<double>();
        if (gyro) gyro_sensitivity = (float)*gyro;

        auto pspeed = tbl["input"]["pointer_speed"].value<double>();
        if (pspeed) pointer_speed = (float)*pspeed;

        auto pport = tbl["input"]["phone_port"].value<int>();
        if (pport) phone_port = *pport;

        // [hle] section in main config.toml — per-game overrides without assets/
        auto eid = tbl["hle"]["ext_interrupt_dispatch"].value<int64_t>();
        if (eid && *eid != 0) {
            game_profile.ext_interrupt_dispatch = (uint32_t)*eid;
            std::cout << "[Config] ext_interrupt_dispatch = 0x"
                      << std::hex << game_profile.ext_interrupt_dispatch
                      << std::dec << std::endl;
        }

        auto slo = tbl["hle"]["sched_lock_offset"].value<int64_t>();
        if (slo && *slo != 0) {
            game_profile.sched_lock_offset = (uint32_t)*slo;
            std::cout << "[Config] sched_lock_offset = 0x"
                      << std::hex << game_profile.sched_lock_offset
                      << std::dec << std::endl;
        }

    } catch (const toml::parse_error& err) {

        std::cerr << "[Config] Failed to parse config.toml:\n" << err << std::endl;
    }
}

void Config::load_game_profile() {
    // Look for assets/games/<game_id>.toml relative to executable location
    std::vector<std::string> search_paths = {
        "assets/games/" + game_id + ".toml",
        game_dir + "/../assets/games/" + game_id + ".toml",
    };

    std::string found_path;
    for (const auto& p : search_paths) {
        if (std::filesystem::exists(p)) {
            found_path = p;
            break;
        }
    }

    if (found_path.empty()) {
        std::cout << "[Config] No game profile for " << game_id << " (using defaults)." << std::endl;
        return;
    }

    try {
        toml::table tbl = toml::parse_file(found_path);

        auto cst = tbl["quirks"]["callback_stack_top"].value<int64_t>();
        if (cst) {
            game_profile.callback_stack_top = (uint32_t)*cst;
            std::cout << "[Config] Game profile: callback_stack_top = 0x"
                      << std::hex << game_profile.callback_stack_top << std::dec << std::endl;
        }

        if (auto mods = tbl["quirks"]["disable_module"].as_array()) {
            mods->for_each([this](const toml::value<std::string>& v) {
                game_profile.disable_modules.push_back(*v);
                std::cout << "[Config] Game profile: disable_module = " << *v << std::endl;
            });
        }

        auto rpl = tbl["game"]["rpl_path"].value<std::string>();
        if (rpl) game_profile.rpl_path = *rpl;

        auto eid = tbl["hle"]["ext_interrupt_dispatch"].value<int64_t>();
        if (eid) {
            game_profile.ext_interrupt_dispatch = (uint32_t)*eid;
            std::cout << "[Config] Game profile: ext_interrupt_dispatch = 0x"
                      << std::hex << game_profile.ext_interrupt_dispatch
                      << std::dec << std::endl;
        }


        std::cout << "[Config] Loaded game profile: " << found_path << std::endl;

    } catch (const toml::parse_error& err) {
        std::cerr << "[Config] Failed to parse game profile " << found_path
                  << ":\n" << err << std::endl;
    }
}

} // namespace runtime
} // namespace nwii

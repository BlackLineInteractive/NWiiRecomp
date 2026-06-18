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

    } catch (const toml::parse_error& err) {
        std::cerr << "[Config] Failed to parse config.toml:\n" << err << std::endl;
    }
}

} // namespace runtime
} // namespace nwii

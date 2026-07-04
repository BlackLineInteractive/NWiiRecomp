#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include "loader/loader.h"
#include "analyzer/analyzer.h"
#include "recompiler/recompiler.h"

#include <toml++/toml.hpp>

int main(int argc, char** argv) {
    std::string config_path = (argc >= 2) ? argv[1] : "recomp_config.toml";
    
    nwii::recomp::RecompilerConfig config;
    try {
        auto tbl = toml::parse_file(config_path);
        config.project_name = tbl["project_name"].value_or("RecompiledGame");
        config.input_game_dir = tbl["input_game_dir"].value_or(".");
        config.output_dir = tbl["output_dir"].value_or("export");
        config.runtime_source_dir = tbl["runtime_source_dir"].value_or("../nWiiRuntime");
        config.symbols_csv = tbl["symbols_csv"].value_or("");
        
        config.split_output = tbl["split_output"].value_or(false);
        config.instructions_per_file = tbl["instructions_per_file"].value_or(20000);

        // [hle_hooks]: "80242550" = "IOS_Open" (per-game, optional)
        if (auto hooks = tbl["hle_hooks"].as_table()) {
            for (const auto& [key, val] : *hooks) {
                uint32_t addr = (uint32_t)std::stoul(std::string(key.str()), nullptr, 16);
                std::string name = val.value_or(std::string());
                if (addr != 0 && !name.empty()) {
                    config.hle_hooks[addr] = name;
                }
            }
            std::cout << "Loaded " << config.hle_hooks.size() << " HLE hooks from config.\n";
        }
        std::cout << "Loaded config from " << config_path << "\n";
    } catch (const toml::parse_error& err) {
        std::cerr << "Config file " << config_path << " not found or invalid.\n";
        return 1;
    }

    std::cout << "NWiiRecomp: loading game from " << config.input_game_dir << "\n";
    nwii::loader::Executable exec;
    if (!exec.load_unpacked_game(config.input_game_dir)) {
        std::cerr << "Failed to load executable.\n";
        return 1;
    }

    std::cout << "Successfully loaded DOL.\n";
    std::cout << "Entry point: 0x" << std::hex << (uint32_t)exec.entry_point << std::dec << "\n";
    std::cout << "Sections loaded: " << exec.sections.size() << "\n";

    nwii::recomp::SymbolTable symbols;
    bool has_symbols = false;
    std::vector<uint32_t> symbol_addresses;

    if (!config.symbols_csv.empty()) {
        std::cout << "Loading symbols from " << config.symbols_csv << "...\n";
        if (symbols.load_csv(config.symbols_csv)) {
            std::cout << "Symbols loaded successfully.\n";
            has_symbols = true;
            for (const auto& [addr, name] : symbols.get_all_symbols()) {
                symbol_addresses.push_back(addr);
            }
        } else {
            std::cerr << "Failed to load symbols.\n";
        }
    }

    std::cout << "Running Code Analyzer...\n";
    nwii::analyzer::Analyzer analyzer(exec);
    analyzer.analyze(symbol_addresses);

    const auto& functions = analyzer.get_functions();
    std::cout << "Analysis complete. Discovered " << functions.size() << " functions.\n";

    // Optional Dolphin signature database (totaldb.dsy): recovers SDK
    // function names for any game. Configured via sdk_sigdb in the TOML.
    std::string sigdb_path;
    try {
        auto tbl = toml::parse_file(config_path);
        sigdb_path = tbl["sdk_sigdb"].value_or(std::string());
    } catch (...) {}
    std::string sdk_report;
    if (!sigdb_path.empty()) {
        if (analyzer.apply_signature_db(sigdb_path) > 0) {
            sdk_report = "Name,Location,Size\n";
            char line[192];
            for (const auto& [addr, func] : functions) {
                if (func.sdk_name.empty())
                    continue;
                uint32_t size = (uint32_t)func.instructions.size() * 4;
                std::snprintf(line, sizeof(line), "\"%s\",%08X,%u\n",
                              func.sdk_name.c_str(), addr, size);
                sdk_report += line;
                // Readable, collision-free symbol: Name_ADDR. The suffix
                // keeps auto-HLE name matching out of the loop; hooking
                // stays an explicit per-game [hle_hooks] decision.
                std::string ident = func.sdk_name;
                for (char& c : ident)
                    if (!std::isalnum((unsigned char)c) && c != '_')
                        c = '_';
                char suffix[16];
                std::snprintf(suffix, sizeof(suffix), "_%08X", addr);
                symbols.add_symbol(addr, ident + suffix);
            }
            has_symbols = true;
        }
    }

    std::cout << "Generating CMake Project...\n";
    nwii::recomp::Recompiler recompiler(analyzer, has_symbols ? &symbols : nullptr, config);
    if (recompiler.generate_cmake_project(exec.entry_point)) {
        std::cout << "Successfully exported standalone project to '" << config.output_dir << "' directory!\n";
        if (!sdk_report.empty()) {
            std::ofstream report(config.output_dir + "/sdk_symbols.csv");
            report << sdk_report;
            std::cout << "SDK symbol report: " << config.output_dir
                      << "/sdk_symbols.csv\n";
        }
    } else {
        std::cerr << "Failed to export project.\n";
    }

    return 0;
}


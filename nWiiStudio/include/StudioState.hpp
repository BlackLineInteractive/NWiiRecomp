#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <future>
#include <atomic>
#include <mutex>
#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>

#include "loader/loader.h"
#include "analyzer/analyzer.h"
#include "recompiler/recompiler.h"
#include "recompiler/symbols.h"

enum class OverrideStatus {
    Default, Stub, Skip, ForceRecompile
};

enum class ThemeMode {
    Dark, Light, Custom
};

struct AppSettings {
    ThemeMode theme = ThemeMode::Dark;
    float fontSize = 15.0f;
    float uiScale = 1.0f;
    std::string selectedFont = "Font_1.ttf";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool maximized = true;

    // Custom theme colors
    float customBgBase[4] = {0.08f, 0.08f, 0.08f, 1.00f};
    float customAccent[4] = {0.00f, 0.48f, 0.80f, 1.00f};
};

class StreamRedirector : public std::stringbuf {
public:
    StreamRedirector(std::vector<std::string>& target, std::mutex& mtx, std::atomic<size_t>& ver)
        : logs(target), mutex(mtx), version(ver) {}
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::string line = this->str();
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty()) {
            logs.push_back(line);
            version.fetch_add(1);
        }
        this->str("");
        return 0;
    }
private:
    std::vector<std::string>& logs;
    std::mutex& mutex;
    std::atomic<size_t>& version;
};

struct UIState {
    std::string unpackedGamePath;
    std::string outputPath = "output";
    std::string customOutputPath;
    std::string configTomlContent;
    
    // Loaded DOL Data for Hex View
    std::vector<uint8_t> rawExecutableData;
    
    std::map<uint32_t, OverrideStatus> funcOverrides;
    
    std::unique_ptr<nwii::loader::Executable> executable;
    std::unique_ptr<nwii::analyzer::Analyzer> analyzer;
    bool isAnalysisComplete = false;
};

class StudioState {
public:
    UIState data;
    AppSettings settings;
    
    // Address of the selected function
    uint32_t selectedFuncAddress = 0;
    
    std::atomic<bool> isBusy = false;
    std::vector<std::string> logs;
    std::mutex stateMutex;
    std::streambuf* oldCout = nullptr;
    std::streambuf* oldCerr = nullptr;
    std::unique_ptr<StreamRedirector> redirector;
    std::future<void> workerThread;
    std::vector<std::string> availableFonts;

    std::atomic<bool> pendingFontRebuild{false};
    std::string configTomlPath;
    std::atomic<size_t> logVersion{0};
    
    nwii::recomp::SymbolTable symbolTable;
    std::string symbolsPath;

    void SetStatus(const std::string& msg) {
        std::lock_guard<std::mutex> lock(statusMutex_);
        statusMessage_ = msg;
    }
    std::string GetStatus() const {
        std::lock_guard<std::mutex> lock(statusMutex_);
        return statusMessage_;
    }

    StudioState() {
        redirector = std::make_unique<StreamRedirector>(logs, stateMutex, logVersion);
        oldCout = std::cout.rdbuf(redirector.get());
        oldCerr = std::cerr.rdbuf(redirector.get());

        try {
            if (!std::filesystem::exists("output")) {
                std::filesystem::create_directory("output");
            }
            data.outputPath = std::filesystem::absolute("output").string();
        } catch(...) {}

        LoadSettings();
        ScanFonts();
        LoadConfigToml();
    }

    ~StudioState() {
        std::cout.rdbuf(oldCout);
        std::cerr.rdbuf(oldCerr);
        SaveSettings();
    }

    void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(stateMutex);
        logs.push_back("[Studio] " + msg);
        logVersion.fetch_add(1);
    }

    void LogRaw(const std::string& msg) {
        std::lock_guard<std::mutex> lock(stateMutex);
        logs.push_back(msg);
        logVersion.fetch_add(1);
    }

    void ScanFonts() {
        availableFonts.clear();
        try {
            std::filesystem::path fontDir = "external/Font";
            if (std::filesystem::exists(fontDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(fontDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".ttf" || ext == ".TTF") {
                            availableFonts.push_back(entry.path().filename().string());
                        }
                    }
                }
            }
        } catch(...) {}

        if (availableFonts.empty()) {
            availableFonts.push_back("Default");
        }
    }

    void LoadSettings() {
        // Simple loading logic
    }

    void SaveSettings() {
        // Simple saving logic
    }

    void LoadUnpackedGame(const std::string& path) {
        data.unpackedGamePath = path;
        data.isAnalysisComplete = false;
        data.analyzer.reset();
        data.executable.reset();
        data.funcOverrides.clear();

        SetStatus("Loading Game...");
        Log("Attempting to load game from: " + path);

        try {
            auto exec = std::make_unique<nwii::loader::Executable>();
            if (exec->load_unpacked_game(path)) {
                data.executable = std::move(exec);
                
                // Load DOL file into raw data for the Hex Viewer
                std::string dol_path = path + "/sys/main.dol";
                std::ifstream file(dol_path, std::ios::binary);
                if (!file.good()) {
                    dol_path = path + "/main.dol";
                    file.open(dol_path, std::ios::binary);
                }
                
                if (file.good()) {
                    data.rawExecutableData.assign((std::istreambuf_iterator<char>(file)), 
                                                  (std::istreambuf_iterator<char>()));
                }

                SetStatus("Loaded: " + dol_path);
                Log("Successfully loaded DOL. Entry point: " + std::to_string(data.executable->entry_point));
            } else {
                SetStatus("Failed to load");
                Log("Failed to load unpacked game (could not find main.dol)");
            }
        } catch(const std::exception& e) {
            SetStatus("Error");
            Log("Exception during load: " + std::string(e.what()));
        }
    }

    void SetOutputDir(const std::string& path) {
        if (path.empty()) return;
        try {
            data.customOutputPath = std::filesystem::absolute(path).string();
            if (!std::filesystem::exists(data.customOutputPath)) {
                std::filesystem::create_directories(data.customOutputPath);
            }
            Log("Custom output directory set to: " + data.customOutputPath);
        } catch (const std::exception& e) {
            Log(std::string("Error setting output dir: ") + e.what());
            data.customOutputPath = std::filesystem::absolute("output").string();
        }
    }

    void StartAnalysis() {
        if (isBusy || !data.executable) return;

        isBusy = true;
        SetStatus("Analyzing PowerPC Code...");

        workerThread = std::async(std::launch::async, [this]() {
            try {
                auto newAnalyzer = std::make_unique<nwii::analyzer::Analyzer>(*data.executable);
                newAnalyzer->analyze();
                
                std::lock_guard<std::mutex> lock(stateMutex);
                data.analyzer = std::move(newAnalyzer);
                data.isAnalysisComplete = true;
                SetStatus("Analysis Complete");
                Log("Discovered " + std::to_string(data.analyzer->get_functions().size()) + " functions.");
                logVersion.fetch_add(1);
            } catch (const std::exception& e) {
                SetStatus("Error"); 
                Log(std::string("Analysis exception: ") + e.what());
            }
            isBusy = false;
        });
    }

    void StartRecompilation() {
        if (isBusy || !data.analyzer || !data.isAnalysisComplete) return;

        isBusy = true;
        SetStatus("Generating C++ Code...");

        workerThread = std::async(std::launch::async, [this]() {
            try {
                nwii::recomp::Recompiler recompiler(*data.analyzer, &symbolTable);
                std::string out_dir = GetEffectiveOutputPath();
                std::string runtime_src = "/Users/vovavovchok/NWiiRecomp/nWiiRuntime"; // Hardcoded for this development environment
                if (recompiler.generate_cmake_project(out_dir, runtime_src, data.executable->entry_point)) {
                    SetStatus("C++ Generation Complete");
                    Log("Successfully generated standalone CMake project at: " + out_dir);
                } else {
                    SetStatus("Error generating C++");
                    Log("Failed to generate CMake project at " + out_dir);
                }
            } catch (const std::exception& e) {
                SetStatus("Error"); 
                Log(std::string("Recompilation exception: ") + e.what());
            }
            isBusy = false;
        });
    }

    void LoadGhidraCSV(const std::string& path) {
        if (symbolTable.load_csv(path)) {
            symbolsPath = path;
            Log("Successfully loaded symbols from: " + path);
        } else {
            Log("Failed to load symbols from: " + path);
        }
    }

    void LoadConfigToml() {
        // Minimal stub
    }

    void CreateDefaultConfig() {
        // Minimal stub
    }

    void SaveConfigTOML() {
        // Minimal stub
    }

    void SaveConfigTomlFromEditor(const std::string& newContent) {
        // Minimal stub
    }

    std::string GetEffectiveOutputPath() const {
        if (!data.customOutputPath.empty()) return data.customOutputPath;
        return data.outputPath;
    }

private:
    mutable std::mutex statusMutex_;
    std::string statusMessage_ = "Ready";
};
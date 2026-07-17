#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nwii {
namespace analyzer {

// Loader/matcher for Dolphin signature databases (Sys/totaldb.dsy).
// The checksum algorithm is a faithful port of Dolphin's

// for Dolphin identifies SDK functions in our analyzed DOLs too.
class DolphinSigDB {
public:
    struct Entry {
        std::string name;
        uint32_t size;
    };

    // char name[128]} records (native little-endian, as Dolphin writes).
    bool load_dsy(const std::string& path);

    // Dolphin-compatible checksum over a function's opcodes.
    static uint32_t checksum(const std::vector<uint32_t>& opcodes);

    const Entry* match(uint32_t checksum) const;

    size_t size() const { return db_.size(); }

private:
    std::map<uint32_t, Entry> db_;
};

} 
} 

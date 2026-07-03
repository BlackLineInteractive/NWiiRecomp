#include "analyzer/signature_scanner.h"
#include <iomanip>
#include <sstream>

namespace nwii {
namespace analyzer {

SignatureScanner::SignatureScanner() {
    signatures_ = {
        { "OSReport", { "9421FF80", "7C0802A6", "9001????", "40860024", "D8210028", "D8410030", "D8610038" } },
        { "IOS_Open", { "9421FFE0", "7C0802A6", "9001????", "34010008", "93E1????", "93C1????", "3BC0????", "93A1????" } },
        { "IOS_IoctlAsync", { "9421FFC0", "7C0802A6", "9001????", "3961????", "48??????", "34010008", "7C771B78" } },
        { "IOS_Ioctl", { "9421FFD0", "7C0802A6", "9001????", "3961????", "48??????", "34010008", "7C791B78" } },
        { "IOS_IoctlvAsync", { "9421FF90", "7C0802A6", "9001????", "93E1????", "7CBF2B78", "93C1????", "7C9E2378" } },
        { "IOS_Ioctlv", { "9421FF90", "7C0802A6", "9001????", "3961????", "48??????", "7C7B1B78", "7C9C2378", "7CBD2B78" } }
        // Removed overly generic patterns that hijacked real game code:
        // - DVD_Callback / VIInit / VIConfigure / VIConfigurePan: matched
        //   the SHSM DVD state machine (0x80211060) and VI init, replacing
        //   them with no-op stubs and killing the boot chain.
        // - iosAlloc / iosFree / IOS_OpenAsync / IOS_Close / IOS_Read:
        //   plain-prologue patterns, matched 11 unrelated functions in the
        //   NFS HP2 (GameCube) build. Per-game [hle_hooks] in the config
        //   TOML covers these precisely where needed.
    };
}

std::string SignatureScanner::match(const std::vector<uint32_t>& instructions) const {
    for (const auto& sig : signatures_) {
        if (instructions.size() < sig.instructions.size()) {
            continue;
        }

        bool matched = true;
        for (size_t i = 0; i < sig.instructions.size(); ++i) {
            std::stringstream ss;
            ss << std::uppercase << std::setfill('0') << std::setw(8) << std::hex << instructions[i];
            std::string hex_inst = ss.str();
            const std::string& pattern = sig.instructions[i];

            for (size_t j = 0; j < 8; ++j) {
                if (pattern[j] != '?' && pattern[j] != hex_inst[j]) {
                    matched = false;
                    break;
                }
            }
            if (!matched) break;
        }

        if (matched) {
            return sig.name;
        }
    }
    return "";
}

} // namespace analyzer
} // namespace nwii

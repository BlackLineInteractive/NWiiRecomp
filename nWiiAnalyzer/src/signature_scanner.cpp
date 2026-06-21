#include "analyzer/signature_scanner.h"
#include <iomanip>
#include <sstream>

namespace nwii {
namespace analyzer {

SignatureScanner::SignatureScanner() {
    signatures_ = {
        { "OSReport", { "9421FF80", "7C0802A6", "9001????", "40860024", "D8210028", "D8410030", "D8610038" } },
        { "IOS_Open", { "9421FFE0", "7C0802A6", "9001????", "34010008", "93E1????", "93C1????", "3BC0????", "93A1????" } },
        { "IOS_OpenAsync", { "8081????", "90A4????", "9003????", "90A3????", "2C1E0000", "4082007C", "83E1????" } },
        { "iosAlloc", { "9421FFE0", "7C0802A6", "9001????", "93E1????", "3BE0????", "93C1????", "7CBE2B78", "93A1????" } },
        // Another common layout for iosAlloc
        { "iosAlloc", { "48??????", "00000000", "00000000", "00000000", "9421FFE0", "7C0802A6", "9001????" } },
        { "iosFree", { "9421FFE0", "7C0802A6", "9001????", "93E1????", "3BE0????", "93C1????", "7C9E2378", "93A1????" } },
        { "IOS_Close", { "93A4????", "80A6????", "8086????", "3805????", "5405073E", "90A6????", "3804????" } },
        { "IOS_Read", { "41820048", "8004????", "3BE0????", "2C000000", "41820038", "8004????", "3BE0????" } },
        { "IOS_IoctlAsync", { "9421FFC0", "7C0802A6", "9001????", "3961????", "48??????", "34010008", "7C771B78" } },
        { "IOS_Ioctl", { "9421FFD0", "7C0802A6", "9001????", "3961????", "48??????", "34010008", "7C791B78" } },
        { "IOS_IoctlvAsync", { "9421FF90", "7C0802A6", "9001????", "93E1????", "7CBF2B78", "93C1????", "7C9E2378" } },
        { "IOS_Ioctlv", { "9421FF90", "7C0802A6", "9001????", "3961????", "48??????", "7C7B1B78", "7C9C2378", "7CBD2B78" } },
        { "DVD_Callback", { "8004????", "2C000000", "41820008", "48??????", "3C80????", "3C60????", "3884????" } },
        { "VIConfigure", { "9421FFE0", "7C0802A6", "3C80????", "9001????", "93E1????", "547F07BE", "93C1????" } },
        { "VIConfigurePan", { "9421FFD0", "7C0802A6", "9001????", "3961????", "48??????", "800D????", "2C000000" } },
        { "VIInit", { "9421FD10", "7C0802A6", "9001????", "3961????", "48??????", "3CA0????", "3FC0????" } }
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

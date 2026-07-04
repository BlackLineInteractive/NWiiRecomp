#include "analyzer/dolphin_sigdb.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace nwii {
namespace analyzer {

bool DolphinSigDB::load_dsy(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), 4);
    if (!f || count == 0 || count > 2000000)
        return false;

    struct FuncDesc {
        uint32_t checksum;
        uint32_t size;
        char name[128];
    } rec;

    for (uint32_t i = 0; i < count && f; ++i) {
        std::memset(&rec, 0, sizeof(rec));
        f.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!f)
            break;
        rec.name[sizeof(rec.name) - 1] = 0;
        db_[rec.checksum] = {rec.name, rec.size};
    }

    std::cout << "[SigDB] Loaded " << db_.size() << " signatures from "
              << path << std::endl;
    return !db_.empty();
}

// Port of Dolphin HashSignatureDB::ComputeCodeChecksum: only opcode bits
// survive, operand fields that change between links are masked out.
uint32_t DolphinSigDB::checksum(const std::vector<uint32_t>& opcodes) {
    uint32_t sum = 0;
    for (uint32_t opcode : opcodes) {
        uint32_t op = opcode & 0xFC000000;
        uint32_t op2 = 0;
        uint32_t op3 = 0;
        uint32_t auxop = op >> 26;
        switch (auxop) {
        case 4: // paired singles
            op2 = opcode & 0x0000003F;
            switch (op2) {
            case 0:
            case 8:
            case 16:
            case 21:
            case 22:
                op3 = opcode & 0x000007C0;
            }
            break;
        case 7: // addi/muli family
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            op2 = opcode & 0x03FF0000;
            break;
        case 19: // branch unit / CR
        case 31: // integer
        case 63: // fpu double
            op2 = opcode & 0x000007FF;
            break;
        case 59: // fpu single
            op2 = opcode & 0x0000003F;
            if (op2 < 16)
                op3 = opcode & 0x000007C0;
            break;
        default:
            if (auxop >= 32 && auxop < 56) // loads/stores
                op2 = opcode & 0x03FF0000;
            break;
        }
        sum = (((sum << 17) & 0xFFFE0000) | ((sum >> 15) & 0x0001FFFF));
        sum = sum ^ (op | op2 | op3);
    }
    return sum;
}

const DolphinSigDB::Entry* DolphinSigDB::match(uint32_t checksum) const {
    auto it = db_.find(checksum);
    return it != db_.end() ? &it->second : nullptr;
}

} // namespace analyzer
} // namespace nwii

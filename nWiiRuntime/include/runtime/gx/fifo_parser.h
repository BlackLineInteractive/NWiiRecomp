#pragma once
#include <vector>
#include <cstdint>
#include "runtime/gx_command.h"

namespace nwii::runtime::gx {

void ApplyBPRegister(uint8_t reg, uint32_t val);

class FifoParser {
public:

    
    static void Parse(std::vector<uint8_t>& fifo, std::vector<GXCommand>& commands);
};

} 

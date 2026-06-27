#pragma once
#include <vector>
#include <cstdint>
#include "runtime/gx_command.h"

namespace nwii::runtime::gx {

class FifoParser {
public:
    // Parses the hardware FIFO buffer and appends generated GXCommands to the queue.
    // Also modifies offset to point to the end of the parsed data.
    // Removes parsed data from the fifo.
    static void Parse(std::vector<uint8_t>& fifo, std::vector<GXCommand>& commands);
};

} // namespace nwii::runtime::gx

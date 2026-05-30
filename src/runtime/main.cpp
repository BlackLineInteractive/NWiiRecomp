#include <iostream>

namespace nwii::runtime {

bool init() {
    std::cout << "init runtime\n";
    // TODO: setup hle, gfx, input
    return true;
}

void shutdown() {
    std::cout << "shutdown runtime\n";
}

} // namespace nwii::runtime

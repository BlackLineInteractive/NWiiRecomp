#include "runtime/cpu_context.h"
#include <iostream>
#include <raylib.h>

using namespace nwii::runtime;

extern "C" {

void PADInit(CPUContext& ctx) {
    std::cout << "[HLE PAD] PADInit: Initializing GameCube Controllers" << std::endl;
}

void PADRead(CPUContext& ctx) {
    // uint32_t pad_status_array_addr = ctx.gpr[3];
    // In a real HLE, we would read IsGamepadButtonDown(0, ...) and write to pad_status_array
    
    // std::cout << "[HLE PAD] PADRead" << std::endl;
    // We shouldn't spam cout for every frame, PADRead is called 60 times a second.
}

void WPADInit(CPUContext& ctx) {
    std::cout << "[HLE WPAD] WPADInit: Initializing Wii Remotes" << std::endl;
}

void WPADRead(CPUContext& ctx) {
    // uint32_t chan = ctx.gpr[3];
    // uint32_t wpad_data_addr = ctx.gpr[4];
}

void KPADInit(CPUContext& ctx) {
    std::cout << "[HLE KPAD] KPADInit" << std::endl;
}

} // extern "C"

#include "runtime/cpu_context.h"
#include <iostream>

using namespace nwii::runtime;

extern "C" {

void AXInit(CPUContext& ctx) {
    std::cout << "[HLE AX] AXInit: Initializing Audio subsystem" << std::endl;
}


void AXAcquireVoice(CPUContext& ctx) {
    uint32_t priority = ctx.gpr[3];
    uint32_t callback = ctx.gpr[4];
    uint32_t user_data = ctx.gpr[5];
    
    std::cout << "[HLE AX] AXAcquireVoice: prio=" << priority 
              << ", cb=" << std::hex << callback << std::dec << std::endl;

    ctx.gpr[3] = 0x1000;
}


void AXFreeVoice(CPUContext& ctx) {
    uint32_t voice_ptr = ctx.gpr[3];
    std::cout << "[HLE AX] AXFreeVoice: voice=" << std::hex << voice_ptr << std::dec << std::endl;
}


void AXSetVoiceState(CPUContext& ctx) {
    uint32_t voice_ptr = ctx.gpr[3];
    uint32_t state = ctx.gpr[4];
    
    std::cout << "[HLE AX] AXSetVoiceState: voice=" << std::hex << voice_ptr 
              << ", state=" << std::dec << state << std::endl;
}


void AXSetVoiceMix(CPUContext& ctx) {
    uint32_t voice_ptr = ctx.gpr[3];
    std::cout << "[HLE AX] AXSetVoiceMix: voice=" << std::hex << voice_ptr << std::dec << std::endl;
}


void AXSetVoiceAdpcm(CPUContext& ctx) {
    
}


void AXSetVoiceSrc(CPUContext& ctx) {
    
}


void AXSetVoiceOffsets(CPUContext& ctx) {
    
}

} 

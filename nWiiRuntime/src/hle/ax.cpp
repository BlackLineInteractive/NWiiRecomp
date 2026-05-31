#include "runtime/cpu_context.h"
#include <iostream>

using namespace nwii::runtime;

extern "C" {

// Initialize the AX (Audio) subsystem
void AXInit(CPUContext& ctx) {
    std::cout << "[HLE AX] AXInit: Initializing Audio subsystem" << std::endl;
}

// Allocate a voice (channel) for playback
// Returns voice ID or nullptr equivalent
void AXAcquireVoice(CPUContext& ctx) {
    uint32_t priority = ctx.gpr[3];
    uint32_t callback = ctx.gpr[4];
    uint32_t user_data = ctx.gpr[5];
    
    std::cout << "[HLE AX] AXAcquireVoice: prio=" << priority 
              << ", cb=" << std::hex << callback << std::dec << std::endl;
              
    // Stub: return a dummy voice pointer (e.g., 0x1000)
    ctx.gpr[3] = 0x1000;
}

// Free a previously allocated voice
// r3 = voice pointer
void AXFreeVoice(CPUContext& ctx) {
    uint32_t voice_ptr = ctx.gpr[3];
    std::cout << "[HLE AX] AXFreeVoice: voice=" << std::hex << voice_ptr << std::dec << std::endl;
}

// Set the current voice state (play, stop, etc.)
// r3 = voice pointer, r4 = state
void AXSetVoiceState(CPUContext& ctx) {
    uint32_t voice_ptr = ctx.gpr[3];
    uint32_t state = ctx.gpr[4];
    
    std::cout << "[HLE AX] AXSetVoiceState: voice=" << std::hex << voice_ptr 
              << ", state=" << std::dec << state << std::endl;
}

// Set volume/mix for a voice
// r3 = voice pointer
void AXSetVoiceMix(CPUContext& ctx) {
    uint32_t voice_ptr = ctx.gpr[3];
    std::cout << "[HLE AX] AXSetVoiceMix: voice=" << std::hex << voice_ptr << std::dec << std::endl;
}

// Set ADPCM parameters for a voice
// r3 = voice pointer, r4 = adpcm struct
void AXSetVoiceAdpcm(CPUContext& ctx) {
    // Stub
}

// Set source sample offsets
// r3 = voice pointer, r4 = offset struct
void AXSetVoiceSrc(CPUContext& ctx) {
    // Stub
}

// Set playback offsets (start, loop, end)
// r3 = voice pointer, r4 = format, r5 = loop_addr, r6 = loop_end_addr, r7 = current_addr
void AXSetVoiceOffsets(CPUContext& ctx) {
    // Stub
}

} // extern "C"

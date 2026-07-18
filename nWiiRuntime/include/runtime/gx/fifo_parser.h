#pragma once
#include <vector>
#include <cstdint>
#include "runtime/gx_command.h"
#include "runtime/gx_state.h"

namespace nwii::runtime::gx {

void ApplyBPRegister(uint8_t reg, uint32_t val);

// Drain profiling counters (defined in fifo_parser.cpp, reported by gx.cpp).
extern uint64_t g_prof_draws, g_prof_draw_bytes, g_prof_dl_calls,
    g_prof_dl_bytes, g_prof_cmds, g_prof_unknown, g_prof_dl_us, g_prof_snap_us;


// Parse-time snapshot of everything needed to decode a draw's raw vertex
// bytes later (after frame-skip decides the draw survives).
struct DrawRaw {
    VATSlot vat;
    uint32_t arrayBase[16];
    uint32_t arrayStride[16];
    uint8_t defPosMtxIdx;
    bool need_normal;   // lighting enabled at snapshot time
    uint16_t count;
    std::vector<uint8_t> bytes;
};

class FifoParser {
public:


    static void Parse(std::vector<uint8_t>& fifo, std::vector<GXCommand>& commands);

    // Decode a deferred draw's raw bytes into cmd.vertices using the state
    // snapshotted at parse time. No-op if already decoded or not a draw.
    static void DecodeDraw(GXCommand& cmd);
};

} 

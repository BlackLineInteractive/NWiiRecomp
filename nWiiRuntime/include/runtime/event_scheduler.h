#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace nwii::runtime {

struct CPUContext;

// Cycle-accurate event scheduler (the model every serious GC/Wii emulator
// uses: Dolphin CoreTiming). Instead of scattered `inst_count % N == 0`
// probes and wall-clock hacks, there is ONE monotonic tick source (the
// timebase) and a priority queue of future events. Hardware that ticks on
// its own — VI retrace, AI/DSP audio DMA, DI transfer completion — each
// schedules its next fire and reschedules from its own callback.
//
// Ticks are timebase units (bus-clock/4). The scheduler reads the clock
// through CPUContext::read_timebase(), so it paces correctly whether the
// timebase follows guest execution (default) or wall-clock (NWII_WALLTB).
class EventScheduler {
public:
    using EventId = uint64_t;
    // late_ticks = how far past the due time we actually fired (the pump is
    // not infinitely fine-grained); recurring events subtract it to avoid
    // drift.
    using Callback = std::function<void(CPUContext& ctx, uint64_t late_ticks)>;

    static EventScheduler& get();

    // Fire `cb` once, `delay` ticks from now. Returns a handle for Cancel.
    EventId schedule_after(uint64_t delay, Callback cb);

    // Fire `cb` every `period` ticks starting `period` ticks from now. The
    // callback keeps firing until Cancel; period may be 0 to stop.
    EventId schedule_recurring(uint64_t period, Callback cb);

    bool cancel(EventId id);

    // Fire every event whose due tick has passed. Called from the backedge
    // callback pump with the current timebase value.
    void advance(CPUContext& ctx, uint64_t now);

    void reset();
    uint64_t now() const { return m_now; }

private:
    struct Event {
        uint64_t due;
        EventId id;
        uint64_t period;   // 0 = one-shot
        Callback cb;
    };
    // Min-heap ordered by `due` (soonest first), tie-broken by id.
    std::vector<Event> m_heap;
    uint64_t m_now = 0;
    EventId m_next_id = 1;
    bool m_firing = false;

    void push(Event e);
    void sift_up(size_t i);
    void sift_down(size_t i);
};

} // namespace nwii::runtime

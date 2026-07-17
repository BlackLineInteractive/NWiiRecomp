#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace nwii::runtime {

struct CPUContext;

// uses: Dolphin CoreTiming). Instead of scattered `inst_count % N == 0`





class EventScheduler {
public:
    using EventId = uint64_t;

    
    using Callback = std::function<void(CPUContext& ctx, uint64_t late_ticks)>;

    static EventScheduler& get();

    EventId schedule_after(uint64_t delay, Callback cb);

    
    EventId schedule_recurring(uint64_t period, Callback cb);

    bool cancel(EventId id);

    
    void advance(CPUContext& ctx, uint64_t now);

    void reset();
    uint64_t now() const { return m_now; }

private:
    struct Event {
        uint64_t due;
        EventId id;
        uint64_t period;   
        Callback cb;
    };
    
    std::vector<Event> m_heap;
    uint64_t m_now = 0;
    EventId m_next_id = 1;
    bool m_firing = false;

    void push(Event e);
    void sift_up(size_t i);
    void sift_down(size_t i);
};

} 

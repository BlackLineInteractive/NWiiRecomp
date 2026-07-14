#include "runtime/event_scheduler.h"
#include "runtime/cpu_context.h"
#include <algorithm>

namespace nwii::runtime {

EventScheduler& EventScheduler::get() {
    static EventScheduler instance;
    return instance;
}

void EventScheduler::sift_up(size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (m_heap[parent].due < m_heap[i].due ||
            (m_heap[parent].due == m_heap[i].due && m_heap[parent].id <= m_heap[i].id))
            break;
        std::swap(m_heap[parent], m_heap[i]);
        i = parent;
    }
}

void EventScheduler::sift_down(size_t i) {
    size_t n = m_heap.size();
    for (;;) {
        size_t l = 2 * i + 1, r = 2 * i + 2, best = i;
        auto before = [&](size_t a, size_t b) {
            return m_heap[a].due < m_heap[b].due ||
                   (m_heap[a].due == m_heap[b].due && m_heap[a].id < m_heap[b].id);
        };
        if (l < n && before(l, best)) best = l;
        if (r < n && before(r, best)) best = r;
        if (best == i) break;
        std::swap(m_heap[best], m_heap[i]);
        i = best;
    }
}

void EventScheduler::push(Event e) {
    m_heap.push_back(std::move(e));
    sift_up(m_heap.size() - 1);
}

EventScheduler::EventId EventScheduler::schedule_after(uint64_t delay, Callback cb) {
    EventId id = m_next_id++;
    push({m_now + delay, id, 0, std::move(cb)});
    return id;
}

EventScheduler::EventId EventScheduler::schedule_recurring(uint64_t period, Callback cb) {
    EventId id = m_next_id++;
    uint64_t p = period ? period : 1;
    push({m_now + p, id, p, std::move(cb)});
    return id;
}

bool EventScheduler::cancel(EventId id) {
    for (size_t i = 0; i < m_heap.size(); ++i) {
        if (m_heap[i].id == id) {
            m_heap[i] = std::move(m_heap.back());
            m_heap.pop_back();
            if (i < m_heap.size()) {
                sift_up(i);
                sift_down(i);
            }
            return true;
        }
    }
    return false;
}

void EventScheduler::advance(CPUContext& ctx, uint64_t now) {
    if (now < m_now) return; // timebase never runs backwards
    m_now = now;
    if (m_firing) return; // re-entrancy guard (a callback that pumps)
    m_firing = true;
    // Fire in due order. Recurring events are re-pushed with their next due
    // so a slow pump that jumped past several periods emits one catch-up
    // fire per pass rather than looping unboundedly here.
    while (!m_heap.empty() && m_heap.front().due <= m_now) {
        Event e = std::move(m_heap.front());
        m_heap[0] = std::move(m_heap.back());
        m_heap.pop_back();
        if (!m_heap.empty()) sift_down(0);

        uint64_t late = m_now - e.due;
        if (e.period) {
            // Reschedule first so a callback that itself schedules keeps the
            // heap consistent; skip whole missed periods to bound catch-up.
            uint64_t next = e.due + e.period;
            if (next <= m_now) next = m_now + e.period;
            Callback cb = e.cb; // keep for the recurring copy
            push({next, e.id, e.period, cb});
            cb(ctx, late);
        } else {
            e.cb(ctx, late);
        }
    }
    m_firing = false;
}

void EventScheduler::reset() {
    m_heap.clear();
    m_now = 0;
    m_next_id = 1;
    m_firing = false;
}

} // namespace nwii::runtime

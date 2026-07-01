#pragma once
#include <cstdint>
#include <list>
#include <mutex>
#include <iostream>
#include <algorithm>

namespace nwii::runtime {

enum class ThreadState {
    Ready,
    Running,
    Sleeping,
    Suspended,
    Terminated,
};

struct OSThread {
    uint32_t id;
    ThreadState state;
    uint32_t context_addr; // Pointer to the thread's context saved in guest memory
    uint64_t wakeup_time;  // For OSSleepThread based on time
    bool is_vblank_wait;   // For OSSleepThread waiting on VBlank
};

class ThreadManager {
public:
    static ThreadManager& get() {
        static ThreadManager instance;
        return instance;
    }

    uint32_t create_thread(uint32_t context_addr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint32_t new_id = m_next_thread_id++;
        m_threads.push_back({new_id, ThreadState::Ready, context_addr, 0, false});
        std::cout << "[ThreadManager] Created thread with ID " << new_id << std::endl;
        return new_id;
    }

    void sleep_thread(uint32_t thread_id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find_if(m_threads.begin(), m_threads.end(), [thread_id](const OSThread& t) { return t.id == thread_id; });
        if (it != m_threads.end()) {
            it->state = ThreadState::Sleeping;
            it->is_vblank_wait = true;
            std::cout << "[ThreadManager] Thread " << thread_id << " is now sleeping." << std::endl;
        }
    }

    void resume_thread(uint32_t thread_id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find_if(m_threads.begin(), m_threads.end(), [thread_id](const OSThread& t) { return t.id == thread_id; });
        if (it != m_threads.end()) {
            if (it->state == ThreadState::Suspended || it->state == ThreadState::Sleeping) {
                it->state = ThreadState::Ready;
                it->is_vblank_wait = false;
                std::cout << "[ThreadManager] Thread " << thread_id << " is now ready." << std::endl;
            }
        }
    }
    
    void on_vblank() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& thread : m_threads) {
            if (thread.state == ThreadState::Sleeping && thread.is_vblank_wait) {
                thread.state = ThreadState::Ready;
                thread.is_vblank_wait = false;
                std::cout << "[ThreadManager] Woke up thread " << thread.id << " on VBlank." << std::endl;
            }
        }
    }

private:
    ThreadManager() = default;
    std::list<OSThread> m_threads;
    uint32_t m_next_thread_id = 1;
    std::mutex m_mutex;
};

} // namespace nwii::runtime

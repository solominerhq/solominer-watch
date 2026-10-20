#pragma once
#include <atomic>
#include <stdint.h>

namespace device {
enum class SleepRole : uint8_t { Main, Monitor, Stratum, Miner0, Miner1, Diagnostics };
constexpr uint32_t SleepSliceMs = 2000;
constexpr uint32_t SleepPrepareMs = 500;
constexpr uint32_t SleepRetryMs = 60000;
// One atomic word prevents a canceled request from reusing stale acknowledgments.
// Participants arrive ONLY outside hardware, display, job and filesystem locks.
class SleepGate {
    static constexpr uint32_t Requested = 0x80000000u;
    static constexpr uint32_t Everyone = (1u << 6) - 1;
    std::atomic<uint32_t> state_{0};
public:
    bool request() { uint32_t empty=0; return state_.compare_exchange_strong(empty,Requested); }
    bool requested() const { return (state_.load() & Requested) != 0; }
    bool arrive(SleepRole role) {
        const uint32_t bit=1u << static_cast<unsigned>(role);
        uint32_t state=state_.load();
        while (state & Requested) {
            if (state_.compare_exchange_weak(state,state|bit)) return true;
        }
        return false;
    }
    void leave(SleepRole role) { state_.fetch_and(~(1u << static_cast<unsigned>(role))); }
    bool ready() const { return state_.load() == (Requested | Everyone); }
    void release() { state_.fetch_and(~Requested); }
};
class SleepBackoff {
    uint32_t failedAt_=0;
    bool failed_=false;
public:
    bool allowed(uint32_t now) const { return !failed_ || uint32_t(now-failedAt_)>=SleepRetryMs; }
    void failed(uint32_t now) { failed_=true; failedAt_=now; }
    void succeeded() { failed_=false; }
};
}

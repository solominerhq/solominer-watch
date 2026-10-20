#include "device/sleep_policy.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <chrono>
using device::SleepRole;
using device::SleepGate;
int main() {
    SleepGate gate;
    assert(!gate.ready() && !gate.arrive(SleepRole::Main));
    assert(gate.request()); assert(!gate.request());
    for (int i=0;i<5;++i) assert(gate.arrive(static_cast<SleepRole>(i)));
    assert(!gate.ready()); // Missing/blocked task never permits sleep.
    assert(gate.arrive(SleepRole::Diagnostics) && gate.ready());
    gate.release();assert(!gate.ready());
    assert(!gate.request()); // Previous participants must leave first.
    for (int i=0;i<6;++i) gate.leave(static_cast<SleepRole>(i));
    assert(gate.request());gate.release();
    // Repeated cancellation while six tasks concurrently arrive/leave. A new
    // request cannot inherit the parked bits of a prior generation.
    std::atomic<bool> done{false};
    std::vector<std::thread> workers;
    for (int i=0;i<6;++i) workers.emplace_back([&,i]{
        const auto role=static_cast<SleepRole>(i);
        while (!done) {
            if (gate.arrive(role)) {
                while (gate.requested()) std::this_thread::yield();
                gate.leave(role);
            }
            std::this_thread::yield();
        }
    });
    for (int cycle=0;cycle<500;++cycle) {
        while (!gate.request()) std::this_thread::yield();
        if (cycle%2) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while (!gate.ready()) {assert(std::chrono::steady_clock::now()<until);std::this_thread::yield();}
        }
        gate.release();
    }
    done=true;for(auto& worker:workers)worker.join();
    assert(gate.request());gate.release();
    device::SleepBackoff backoff;
    assert(backoff.allowed(0));backoff.failed(0xfffffff0u);
    assert(!backoff.allowed(uint32_t(0xfffffff0u+59999)));
    assert(backoff.allowed(uint32_t(0xfffffff0u+60000)));
    backoff.succeeded();assert(backoff.allowed(0));
    assert(device::SleepSliceMs<6500); // Existing battery estimator's maximum sample gap.
    puts("PASS: six-task sleep barrier, missing participant, cancellation, 500 concurrent cycles, retry rollover and bounded sleep interval");
}

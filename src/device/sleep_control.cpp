#include "sleep_control.h"
#include <Arduino.h>
#include <esp_task_wdt.h>
namespace device {
static SleepGate gate;
static std::atomic<uint32_t> count{0}, sleptMs{0}, failures{0};
static std::atomic<int> lastWake{0}, lastError{0};
bool requestLightSleep() { return gate.request(); }
bool lightSleepReady() { return gate.ready(); }
void releaseLightSleep() { gate.release(); }
void lightSleepCheckpoint(SleepRole role) {
    if (!gate.arrive(role)) return;
    while (gate.requested()) {
        if (role==SleepRole::Miner0 || role==SleepRole::Miner1) esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    gate.leave(role);
}
void recordLightSleep(uint32_t elapsed,int wake,int error) {
    lastWake=wake;lastError=error;
    if (error) ++failures;
    else { ++count; sleptMs.fetch_add(elapsed); }
}
SleepStats lightSleepStats() { return {count.load(),sleptMs.load(),failures.load(),lastWake.load(),lastError.load()}; }
}

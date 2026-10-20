#pragma once
#include <atomic>
#include <stdint.h>
// Device power is the sole writer; mining workers and UI are readers.
// Independent atomic limits prevent data races; thermal limits always take priority.
extern std::atomic<bool> g_singleCoreMode;
extern std::atomic<uint32_t> g_miningDutyCycleMs;
extern std::atomic<uint32_t> g_thermalDutyFloorMs;
extern std::atomic<bool> g_thermalSingleCore;
extern std::atomic<bool> g_thermalPause;
extern std::atomic<int> g_thermalTier;

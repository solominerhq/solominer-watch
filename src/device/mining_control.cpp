#include "mining_control.h"
#include "runtime_policy.h"
std::atomic<bool> g_singleCoreMode{true};
std::atomic<uint32_t> g_miningDutyCycleMs{policy::MINING_GAP_MS};
std::atomic<uint32_t> g_thermalDutyFloorMs{0};
std::atomic<bool> g_thermalSingleCore{false};
std::atomic<bool> g_thermalPause{false};
std::atomic<int> g_thermalTier{0};

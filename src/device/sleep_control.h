#pragma once
#include "sleep_policy.h"
namespace device {
// Cooperative pause; never suspend a task in the middle of a transaction.
void lightSleepCheckpoint(SleepRole role);
bool requestLightSleep();
bool lightSleepReady();
void releaseLightSleep();
struct SleepStats { uint32_t count, milliseconds, failures; int wake, error; };
SleepStats lightSleepStats();
void recordLightSleep(uint32_t elapsed, int wake, int error);
}

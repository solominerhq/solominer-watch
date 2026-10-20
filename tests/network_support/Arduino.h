#pragma once
#include <stdint.h>
uint32_t millis();
#include <algorithm>
using std::min;
using std::max;
uint32_t getCpuFrequencyMhz();
bool setCpuFrequencyMhz(uint32_t mhz);
float temperatureRead();

#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
inline void portENTER_CRITICAL(portMUX_TYPE* mux){mux->lock();}
inline void portEXIT_CRITICAL(portMUX_TYPE* mux){mux->unlock();}

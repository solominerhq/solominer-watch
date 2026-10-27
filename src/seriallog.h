#pragma once
#include <atomic>
extern std::atomic<bool> g_serialDisabled;
// Normal builds store bounded diagnostics. Enable legacy console chatter only
// for a developer build; arguments are not evaluated when it is disabled.
#ifndef SOLOMINER_VERBOSE_LOGS
#define SOLOMINER_VERBOSE_LOGS 0
#endif
#if SOLOMINER_VERBOSE_LOGS
#define SERIAL_PRINT(...) do { if (!g_serialDisabled.load()) Serial.print(__VA_ARGS__); } while(0)
#define SERIAL_PRINTLN(...) do { if (!g_serialDisabled.load()) Serial.println(__VA_ARGS__); } while(0)
#define SERIAL_PRINTF(...) do { if (!g_serialDisabled.load()) Serial.printf(__VA_ARGS__); } while(0)
#else
#define SERIAL_PRINT(...) do {} while(0)
#define SERIAL_PRINTLN(...) do {} while(0)
#define SERIAL_PRINTF(...) do {} while(0)
#endif

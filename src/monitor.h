#include "network/pool_status.h"
#ifndef MONITOR_API_H
#define MONITOR_API_H

#include <Arduino.h>

// Monitor states
#define SCREEN_MINING   0
#define SCREEN_CLOCK    1
#define NO_SCREEN       2   //Used when board has no TFT

//Time update period
#define UPDATE_PERIOD_h   5

//API public-pool.io (used by getPoolAPIUrl for fallback)
#define getPublicPool "https://public-pool.io:40557/api/client/" // +btcString

enum NMState {
  NM_waitingConfig,
  NM_Connecting,
  NM_hashing
};


typedef struct{
  uint8_t screen;
  bool rotation;
  NMState NerdStatus;
}monitor_data;

typedef struct {
  String completedShares;
  String totalMHashes;
  String totalKHashes;
  String currentHashRate;
  String templates;
  String bestDiff;
  String timeMining;
  String valids;
  String temp;
  String currentTime;
}mining_data;

typedef struct {
  String completedShares;
  String totalKHashes;
  String currentHashRate;
  String btcPrice;
  String currentTime;
  String currentDate;
}clock_data;

typedef struct {
  String currentHashRate;
  String valids;
  unsigned long currentHours;
  unsigned long currentMinutes;
  unsigned long currentSeconds;
}clock_data_t;

void setup_monitor(void);
void updateHashrateSample(uint32_t kh, uint32_t elapsed); // Monitor task, once per actual sample.

// Apply Settings.PosixTZ to the C library (setenv TZ + tzset) so localtime()
// everywhere resolves the correct local time WITH daylight-saving rules. Safe
// to call multiple times; call once early in boot (before the RTC seed) and
// again whenever the timezone setting changes.
void applyTimezone(void);

mining_data getMiningData(unsigned long mElapsed);
clock_data getClockData(unsigned long mElapsed);

clock_data_t getClockData_t(unsigned long mElapsed);
String getPoolAPIUrl(void);

#endif //MONITOR_API_H
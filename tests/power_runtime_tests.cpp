#include <cassert>
#include <cstdio>
#include "device/device_power.h"
#include "device/mining_control.h"
#include "device/runtime_policy.h"
static uint32_t now=0,cpu=240,clockChanges=0;
static float temperature=25;
uint32_t millis(){return now;}
uint32_t getCpuFrequencyMhz(){return cpu;}
bool setCpuFrequencyMhz(uint32_t mhz){cpu=mhz;++clockChanges;return true;}
float temperatureRead(){return temperature;}
static void normalProfile(){assert(cpu==80&&g_singleCoreMode&&g_miningDutyCycleMs==16);}
int main(){
 device::beginPower(false);normalProfile();
 now=60000;assert(!device::batterySaverIdle());
 now=179999;assert(!device::batterySaverIdle());
 now=180000;assert(device::batterySaverIdle());
 device::noteUserActivity();assert(!device::batterySaverIdle());
 now+=179999;assert(!device::batterySaverIdle());
 // An interaction just before the deadline restarts the entire window.
 device::noteUserActivity();
 now+=179999;assert(!device::batterySaverIdle());
 ++now;assert(device::batterySaverIdle());
 device::noteUserActivity();assert(!device::batterySaverIdle());
 // USB never boosts CPU, enables a second worker, or removes the batch gap.
 device::servicePower(true);now+=2000;device::servicePower(true);normalProfile();
 now+=3600000;assert(!device::batterySaverIdle());
 device::requestBatterySaver(false);device::servicePower(true);normalProfile();
 device::requestBatterySaver(true);device::servicePower(true);normalProfile();
 assert(clockChanges==1);
 // Unplug gets a full activity window, then stays paused until real input.
 device::servicePower(false);now+=2000;device::servicePower(false);normalProfile();
 now+=179999;assert(!device::batterySaverIdle());
 ++now;assert(device::batterySaverIdle());
 now+=3600000;assert(device::batterySaverIdle());
 device::requestBatterySaver(false);assert(!device::batterySaverIdle());
 device::requestBatterySaver(true);assert(device::batterySaverIdle());
 device::noteUserActivity();assert(!device::batterySaverIdle());
 // Cold USB boot uses exactly the battery mining profile too.
 device::beginPower(true);normalProfile();
 temperature=80;
 for(int i=0;i<4;++i){now+=5000;device::servicePower(true);}
 assert(g_thermalPause&&g_thermalTier==3&&g_thermalDutyFloorMs==150);
 normalProfile(); // The old USB thermal cap must not raise the CPU to 160 MHz.
 device::servicePower(false);now+=2000;device::servicePower(false);
 assert(g_thermalPause);normalProfile();
 temperature=25;
 for(int i=0;i<30;++i){now+=5000;device::servicePower(false);}
 assert(!g_thermalPause&&g_thermalTier==0);normalProfile();
 now=0xfffffff0u;device::noteUserActivity();
 now+=179999;assert(!device::batterySaverIdle());
 ++now;assert(device::batterySaverIdle());
 // A connection near the deadline grants one full window, not unlimited retries.
 now=1000;device::beginPower(false);device::requestBatterySaver(true);
 now+=170000;assert(device::noteWifiConnected());
 now+=170000;assert(!device::noteWifiConnected());assert(!device::batterySaverIdle());
 now+=10000;assert(device::batterySaverIdle());assert(!device::noteWifiConnected());
 device::noteUserActivity();now+=1000;assert(device::noteWifiConnected());
 now+=180000;assert(device::batterySaverIdle());
 // A never-connected window cannot be resurrected after expiry.
 device::noteUserActivity();now+=180000;assert(!device::noteWifiConnected());
 assert(device::batterySaverIdle());
 // Connection while USB-powered consumes, rather than banks, the allowance.
 device::beginPower(true);assert(!device::noteWifiConnected());
 device::servicePower(false);now+=2000;device::servicePower(false);
 now+=1000;assert(!device::noteWifiConnected());
 // Same rule when Saver is disabled; physical interaction re-arms it.
 device::noteUserActivity();device::requestBatterySaver(false);
 assert(!device::noteWifiConnected());device::requestBatterySaver(true);
 assert(!device::noteWifiConnected());
 now=0xfffffff0u;device::noteUserActivity();now+=1000;
 assert(device::noteWifiConnected());now+=179999;assert(!device::batterySaverIdle());
 ++now;assert(device::batterySaverIdle());
 puts("PASS: one-shot Wi-Fi Saver extension, renewals, late events, USB/Saver-off consumption and rollover");
 puts("PASS: same battery/USB mining profile, activity timeout and wake, saver toggle, USB bypass, timer rollover and thermal protection");
}

#include "seriallog.h"
#include <Arduino.h>
#include "device_power.h"
#include "runtime_policy.h"
#include "mining_control.h"
// One active mining profile regardless of USB power or Battery Saver preference.
// 80 MHz is the Wi-Fi-safe floor; retain the existing battery mining settings.
static std::atomic<bool> saverRequested{true};
static std::atomic<bool> usbApplied{false};
static std::atomic<uint32_t> lastActivity{0};
// Keep the one-shot allowance and timer update indivisible across UI/Wi-Fi tasks.
static portMUX_TYPE activityMux = portMUX_INITIALIZER_UNLOCKED;
static bool connectionExtensionAvailable=true;

static void applyMiningProfile() {
  setCpuFrequencyMhz(policy::MINING_CPU_MHZ);
  g_miningDutyCycleMs = policy::MINING_GAP_MS;
  g_singleCoreMode = true; // hardware worker only on both power sources
}

/******** THERMAL GOVERNOR (skin-safety for a wrist-worn miner) ********/
// Continuous SHA-256 on a wearable needs a temperature ceiling. Every 5s this
// reads the die temperature (median of the last 3 samples, so one noisy reading
// can't flap tiers) and applies graduated CAPS on top of the active power
// profile via the g_thermal* overlays in mining.cpp. The mining loop takes the
// stricter of profile-vs-thermal; the power source never changes the profile.
//
//   Tier 0 NORMAL  (<55°C die): no effect.
//   Tier 1 WARM    (55-65°C):   force single-core + small duty gap.
//   Tier 2 HOT     (65-75°C):   heavy duty gap; CPU stays at the same 80 MHz.
//   Tier 3 CRITICAL(>=75°C):    stop hashing; clock screen shows "Cooling down".
//
// Promotion is immediate; demotion steps down one tier only after the temp has
// stayed 5°C below that tier's entry for 30s (hysteresis — no oscillation).
// These are retained die-temperature thresholds, not measured skin-contact
// temperatures. Verify enclosure temperature and runtime on actual hardware.

#define THERMAL_CHECK_MS       5000UL
#define THERMAL_WARM_C         55.0f
#define THERMAL_HOT_C          65.0f
#define THERMAL_CRIT_C         75.0f
#define THERMAL_HYST_C          5.0f
#define THERMAL_DEMOTE_MS     30000UL
#define THERMAL_WARM_FLOOR_MS    25   // small gap between HW batches
#define THERMAL_HOT_FLOOR_MS    150   // mine a fraction of the time

static void thermalGovernor() {
  static uint32_t lastCheck = 0;
  static float    samples[3] = {0, 0, 0};
  static int      sampleIdx = 0, sampleCount = 0;
  static uint32_t belowSince = 0;
  static bool     belowActive = false;

  if (millis() - lastCheck < THERMAL_CHECK_MS) return;
  lastCheck = millis();

  samples[sampleIdx] = temperatureRead();
  sampleIdx = (sampleIdx + 1) % 3;
  if (sampleCount < 3) { sampleCount++; return; }   // warm-up: need 3 samples
  float a = samples[0], b = samples[1], c = samples[2];
  float t = max(min(a, b), min(max(a, b), c));      // median of 3

  int tier = g_thermalTier;
  int target = (t >= THERMAL_CRIT_C) ? 3 : (t >= THERMAL_HOT_C) ? 2
             : (t >= THERMAL_WARM_C) ? 1 : 0;
  if (target > tier) {
    tier = target;                                   // promote immediately
    belowActive = false;
  } else if (tier > 0) {
    float entry = (tier == 3) ? THERMAL_CRIT_C : (tier == 2) ? THERMAL_HOT_C : THERMAL_WARM_C;
    if (t < entry - THERMAL_HYST_C) {
      if (!belowActive) { belowActive = true; belowSince = millis(); }
      else if (millis() - belowSince >= THERMAL_DEMOTE_MS) { tier--; belowActive = false; }
    } else {
      belowActive = false;
    }
  }

  if (tier != g_thermalTier) {
    SERIAL_PRINTF("[THERMAL] %.1fC — tier %d -> %d\n", t, g_thermalTier.load(), tier);
    g_thermalTier = tier;
  }

  // Thermal caps always take priority over the shared active mining profile.
  g_thermalSingleCore  = (tier >= 1);
  g_thermalDutyFloorMs = (tier >= 2) ? THERMAL_HOT_FLOOR_MS
                       : (tier >= 1) ? THERMAL_WARM_FLOOR_MS : 0;
  g_thermalPause       = (tier >= 3);

}


namespace device {
static policy::UsbDebounce usbState;
void requestBatterySaver(bool enabled) { saverRequested.store(enabled); }
bool batterySaverEnabled() { return saverRequested.load(); }
void noteUserActivity() {
  portENTER_CRITICAL(&activityMux);
  lastActivity.store(millis());
  connectionExtensionAvailable=true;
  portEXIT_CRITICAL(&activityMux);
}
bool noteWifiConnected() {
  portENTER_CRITICAL(&activityMux);
  const uint32_t now=millis();
  // A late callback cannot restart a Saver window that has already expired.
  const bool extend=connectionExtensionAvailable && saverRequested.load() &&
      !usbApplied.load() && !policy::saverIdle(true,false,now,lastActivity.load());
  // Every first connection consumes the allowance, even on USB or with Saver off.
  connectionExtensionAvailable=false;
  if(extend) lastActivity.store(now);
  portEXIT_CRITICAL(&activityMux);
  return extend;
}
bool batterySaverIdle() { return policy::saverIdle(saverRequested.load(),usbApplied.load(),millis(),lastActivity.load()); }
void beginPower(bool usb) {
  portENTER_CRITICAL(&activityMux);
  usbApplied=usb; lastActivity=millis();
  connectionExtensionAvailable=true; // The initial boot window gets one allowance.
  portEXIT_CRITICAL(&activityMux);
  usbState.reset(usb, millis());
  applyMiningProfile();
  thermalGovernor();
}
void servicePower(bool usb) {
  if (usbState.update(usb, millis())) {
    portENTER_CRITICAL(&activityMux);
    usbApplied=usbState.applied();
    if (!usbApplied.load()) lastActivity=millis(); // Fresh window on unplug; no new connection allowance.
    portEXIT_CRITICAL(&activityMux);
    SERIAL_PRINTF("[POWER] %s — same mining profile\n", usbApplied.load() ? "USB" : "Battery");
  }
  thermalGovernor();
}
bool shouldShutdown(int percent, bool absent) {
  static policy::CriticalBattery battery;
  return battery.update(percent, absent);
}
}

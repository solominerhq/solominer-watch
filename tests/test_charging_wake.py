"""Run production charging loop and wake logic against a sleeping panel."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#include <atomic>
#include <cassert>
#include <cstddef>
#define pdTRUE 1
#define portMAX_DELAY 1
#define pdMS_TO_TICKS(value) (value)
#define DISPLAY_WAKE_BRIGHTNESS_DEFAULT 150
#define NAV_CLOCK 0
#define SERIAL_PRINTLN(value) ((void)0)
void* disp_state_mutex = reinterpret_cast<void*>(1);
void* lvgl_mutex = reinterpret_cast<void*>(2);
int xSemaphoreTake(void*, int) { return pdTRUE; }
void xSemaphoreGive(void*) {}
std::atomic<bool> batteryUserWake{false}, panelRedrawRequested{false}, revealAfterRedraw{false};
std::atomic<bool> navigationClickPending{false}, setupButtonsConsumed{false};
bool displayOff=false, displayManuallyOff=false, displayAwake=true, inSetupMode=false;
bool chargingBootMode=true, chargingScreenBuilt=false, pmuInitialized=false;
void *chargingArc=nullptr, *chargingReadingLabel=nullptr, *chargingPctLabel=nullptr;
void *chargingStatus=nullptr, *chargingHint=nullptr;
int chargingTargetPct=-1, currentScreenIndex=2;
unsigned long nowMs=100, lastWakeTime=100;
unsigned long millis() { return nowMs; }
struct { int screenBrightness=150; } Settings;
struct BatterySnapshot { int percent; };
int brightness=150, renderedPct=38, pendingPct=38, steps=0, scenario=0, retries=0;
bool flushed=true;
void setBrightness(int value) {
    if (value) assert(flushed);
    brightness=value;
}
void displayHwWake() { assert(brightness==0); }
void displayHwSleep() { brightness=0; }
void triggerImmediateRedraw() {}
namespace network { void requestReconnect() { ++retries; } }
BatterySnapshot getBatterySnapshot() { return BatterySnapshot{pendingPct}; }
void updateChargingScreen(const BatterySnapshot& battery) { renderedPct=battery.percent; }
void* lv_scr_act() { return nullptr; }
void lv_obj_invalidate(void*) {}
void lv_refr_now(void*) { flushed=true; }
void lv_timer_handler() { flushed=true; }
void buildChargingScreen(const BatterySnapshot& battery) {
    updateChargingScreen(battery); chargingScreenBuilt=true; lastWakeTime=millis();
    lv_refr_now(nullptr);
}
bool usbConfirmedAbsent() { return false; }
void displayDeepSleep() {}
void delay(int) {}
namespace diagnostics { void checkpoint() {} }
struct { void shutdown() {} } pmu;
void invalidateScreen() {}
static void wakeDisplayInternal(bool);
void handleNavigationButton() {
    if (displayOff || displayManuallyOff || !displayAwake) wakeDisplayInternal(true);
}
void vTaskDelay(int) {
    assert(++steps<6);
    if (steps==1) {
        if (scenario==1) { nowMs=30100; return; } // Timed charging sleep.
        displayOff=displayManuallyOff=true; displayAwake=false; displayHwSleep();
    } else if (steps==2 && scenario==1) {
        assert(displayOff && !displayAwake && brightness==0);
    } else {
        if (scenario==2 && steps==2) { // A later user-off must win the wake race.
            assert(brightness==0 && !flushed);
        } else {
            assert(brightness==150 && flushed && renderedPct==37);
            assert(!revealAfterRedraw.load() && !panelRedrawRequested.load());
            assert(retries==0); // Charging wake does not start Wi-Fi early.
            // The lower Power dispatcher (tested separately) permits boot.
            chargingBootMode=false;
            return;
        }
    }
    pendingPct=37; flushed=false;
    wakeDisplayInternal(true);
    assert(brightness==0 && revealAfterRedraw.load());
    if (scenario==2 && steps==1) { displayOff=displayManuallyOff=true; displayAwake=false; }
}
'''


class ChargingWakeTests(unittest.TestCase):
    def test_charging_wake_and_start(self):
        source = (ROOT / 'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        a = source.index('static void wakeDisplayInternal(')
        b = source.index('\nvoid wakeDisplay()', a)
        c = source.index('void chargingBootLoop(void) {')
        d = source.index('\n}\n', c) + 3
        helper = ''
        if 'static void refreshChargingDisplay(' in source:
            h = source.index('static void refreshChargingDisplay(')
            helper = source[h:source.index('\n}\n', h)+3]
        checks = r'''
int main(int argc, char**) {
    scenario=argc-1;
    chargingBootLoop();
    assert(!chargingBootMode && !chargingScreenBuilt && currentScreenIndex==NAV_CLOCK);
    assert(brightness==150 && !displayOff && !displayManuallyOff && displayAwake);
}
'''
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)
            (path / 'test.cpp').write_text(STUBS + source[a:b] + helper + source[c:d] + checks)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                            '-Werror', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            for args in ([], ['timed'], ['manual', 'race']):
                subprocess.run([str(path / 'test'), *args], check=True)

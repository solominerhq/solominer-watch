"""Exercise the actual driver's battery service with a fake PMU and real threads.

Only the hardware/RTOS boundary is replaced. Production cache, register reader,
estimator, getters and USB confirmation code are compiled unchanged.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include "device/battery_reading.h"
#include "ui/battery_notice.h"
#include "device/runtime_policy.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
static std::timed_mutex bus;
static thread_local bool owned = false;
static std::atomic<bool> denyLock{false};
static std::atomic<uint32_t> now{0};
static bool pmuInitialized = true;
uint32_t millis() { return now.load(); }
void delay(uint16_t ms) { now += ms; }
bool i2cLock(uint32_t ms) {
    if (denyLock) return false;
    owned = bus.try_lock_for(std::chrono::milliseconds(ms));
    return owned;
}
void i2cUnlock() { assert(owned); owned = false; bus.unlock(); }
struct FakePmu {
    std::atomic<int> reads{0};
    int shutdowns = 0;
    int failure = -1;
    int registers[256] = {};
    int readRegister(uint8_t reg) {
        assert(owned); ++reads;
        return reg == failure ? -1 : registers[reg];
    }
    void clearIrqStatus() { assert(owned); }
    void shutdown() { assert(owned); ++shutdowns; }
    void set(int gauge, int mv, bool usb, int charge = 5) {
        failure = -1;
        registers[0] = 8 | (usb ? 32 : 0);
        registers[1] = (usb ? (charge < 4 ? 32 : 0) : 64) | charge;
        registers[0x34] = mv >> 8; registers[0x35] = mv & 255;
        registers[0xA4] = gauge;
    }
} pmu;
namespace diagnostics {
static std::atomic<int> reports{0};
static bool plugDuringCheckpoint = false;
void batteryRaw(int, int, int, int) { assert(owned); ++reports; }
void batteryDisplayed(int) { assert(owned); }
void checkpoint() { if (plugDuringCheckpoint) pmu.set(1, 3300, true, 2); }
void usbState(bool) {}
}
namespace device {
bool shouldShutdown(int pct, bool absent) {
    static policy::CriticalBattery cutoff;
    return cutoff.update(pct, absent);
}
}
#define SERIAL_PRINTF(...) do {} while (false)
static bool displayManuallyOff = false, displayOff = false, displayAwake = true;
static bool g_lowBatteryDim = false;
static const int LOW_BATT_BRIGHTNESS_THRESHOLD = 10;
void wakeDisplay() {}
void flushPendingSettings(bool) {}
void checkpointMiningStats() {}
'''

CHECKS = r'''
int main() {
    pmu.set(17, 3515, false);
    // Multiple concurrent consumers perform just one sample per cache window.
    for (uint32_t t = 0; t <= 8000; t += 2000) {
        now = t;
        std::vector<std::thread> callers;
        for (int i = 0; i < 12; ++i) callers.emplace_back([] {
            for (int n = 0; n < 100; ++n) {
                const int pct = getBatteryPercent();
                assert(pct == (now < 8000 ? -1 : 17));
                assert(!isCharging() && !isUSBConnected() && !isChargeDone());
            }
        });
        for (auto& caller : callers) caller.join();
    }
    assert(diagnostics::reports == 5 && pmu.reads == 40);
    pmu.set(17, 3746, true, 2);
    now = 10000;
    assert(getBatteryPercent() == 17);
    assert(isUSBConnected() && isCharging());
    for (now = 12000; now <= 18000; now += 2000) assert(getBatteryPercent() == 17);
    assert(getBatteryPercent() == 17); // voltage rise never becomes 51%
    pmu.set(17, 3515, false);
    for (now = 22000; now <= 32000; now += 2000) {
        assert(getBatteryPercent() == 17);
        assert(!isUSBConnected());
    }
    pmu.set(17, 3746, true, 2);
    now = 34000; assert(getBatteryPercent() == 17 && isUSBConnected());

    // A real lock failure cannot fabricate a depleted cell or an unplug.
    denyLock = true;
    assert(getBatteryPercent() == -1 && isUSBConnected());
    assert(!usbConfirmedAbsent());
    denyLock = false;
    for (int reg : {0x00, 0x01, 0x34, 0x35, 0xA4}) {
        pmu.failure = reg;
        auto failed = getBatterySnapshot(true);
        assert(failed.percent == -1 && failed.sample.shutdownPercent() == -1);
        assert(isUSBConnected());
        assert(!usbConfirmedAbsent());
    }
    pmu.set(1, 3300, false);
    auto low = getBatterySnapshot(true);
    assert(low.percent == -1 && low.sample.shutdownPercent() == 1);
    assert(usbConfirmedAbsent());
    pmu.failure = 0x00; assert(!usbConfirmedAbsent());
    pmu.failure = 0x01; assert(!usbConfirmedAbsent());
    pmu.set(1, 3300, true, 2);
    assert(!usbConfirmedAbsent());
    pmu.registers[1] |= 8; // VINDPM must not look like a USB removal
    assert(getBatterySnapshot(true).sample.usb && !usbConfirmedAbsent());
    assert(!usbConfirmedAbsent(0));

    // Actual cutoff integration: cached UI data cannot suppress protection,
    // a failed read breaks the streak, and plugging in at the final write wins.
    pmu.set(17, 3515, false);
    now += 6000; checkCriticalPower();
    pmu.set(1, 3300, false);
    now += 6000; checkCriticalPower(); assert(pmu.shutdowns == 0);
    pmu.failure = 0xA4;
    now += 6000; checkCriticalPower(); assert(pmu.shutdowns == 0);
    pmu.failure = -1;
    now += 6000; checkCriticalPower(); assert(pmu.shutdowns == 0);
    diagnostics::plugDuringCheckpoint = true;
    now += 6000; checkCriticalPower(); assert(pmu.shutdowns == 0);
    diagnostics::plugDuringCheckpoint = false;
    now += 6000; checkCriticalPower(); assert(pmu.shutdowns == 0);
    pmu.set(1, 3300, false);
    now += 6000; checkCriticalPower(); assert(pmu.shutdowns == 0);
    now += 6000; checkCriticalPower(); assert(pmu.shutdowns == 1);
    assert(!owned); // shutdown must release the bus if hardware returns
}
'''


class BatteryDriverTests(unittest.TestCase):
    def test_shared_service_and_checked_usb(self):
        driver = (ROOT / 'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        start = driver.index('// One synchronized sample/cache/estimator')
        end = driver.index('// =============================================================================\n// SMART DIMMING', start)
        cutoff_start = driver.index('void checkCriticalPower() {')
        cutoff_end = driver.index('\nstatic void wakeDisplayInternal(', cutoff_start)
        with tempfile.TemporaryDirectory(prefix='battery-driver-') as temp:
            source = Path(temp) / 'driver.cpp'
            binary = Path(temp) / 'driver'
            source.write_text(STUBS + driver[start:end] + driver[cutoff_start:cutoff_end] + CHECKS)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-pthread',
                            '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'src'),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == '__main__':
    unittest.main()

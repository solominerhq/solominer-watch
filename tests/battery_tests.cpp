#include "device/battery_reading.h"
#include "device/runtime_policy.h"
#include <array>
#include <cassert>
#include <cstdio>

using battery::Sample;
using battery::Estimator;

static Sample sample(int gauge, int mv = 3800, bool usb = false, int charge = 5) {
    Sample s;
    s.statusValid = s.present = true;
    s.gauge = gauge;
    s.millivolts = mv;
    s.usb = usb;
    s.charge = charge;
    s.direction = usb ? (charge < 4 ? 1 : 0) : 2;
    return s;
}

static int advance(Estimator& e, const Sample& s, uint32_t& now, uint32_t duration,
                   uint32_t interval = 2000) {
    int result = -1;
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += interval) {
        now += interval;
        result = e.update(s, now);
    }
    return result;
}

static void seed(Estimator& e, const Sample& s, uint32_t& now) {
    assert(e.update(s, now) == -1);
    assert(advance(e, s, now, 8000) == s.gauge);
}

static Sample decode(std::array<int, 8> bytes) {
    size_t i = 0;
    const std::array<int, 8> addresses = {{0x00, 0x01, 0x34, 0x35, 0x34, 0xA4, 0x00, 0x01}};
    auto s = battery::readSample([&](uint8_t address) {
        assert(i < bytes.size() && address == addresses[i]);
        return bytes[i++];
    });
    assert(i == bytes.size());
    return s;
}

int main() {
    // Actual register adapter: 3300 mV, 1%, discharge, no USB.
    const std::array<int, 8> low = {{8, 0x45, 12, 228, 12, 1, 8, 0x45}};
    auto s = decode(low);
    assert(s.valid() && s.millivolts == 3300 && s.shutdownPercent() == 1);
    for (size_t i = 0; i < low.size(); ++i) {
        auto bad = low;
        bad[i] = -1;
        auto failed = decode(bad);
        assert(!failed.valid() && failed.shutdownPercent() == -1);
        policy::CriticalBattery cutoff;
        for (int repeat = 0; repeat < 5; ++repeat)
            assert(!cutoff.update(failed.shutdownPercent(), failed.usbAbsent()));
    }
    auto torn = low; torn[6] |= 0x20; assert(!decode(torn).statusValid);
    torn = low; torn[7] = 0x22; assert(!decode(torn).statusValid);
    torn = low; torn[4]++; assert(!decode(torn).valid());
    for (int reserved : {6, 7, 0x65}) {
        auto bad = low; bad[1] = bad[7] = reserved;
        assert(!decode(bad).statusValid);
    }
    for (int gauge : {-1, 101, 255}) {
        s = sample(gauge); assert(!s.valid());
    }
    for (int mv : {-1, 0, 8191}) {
        s = sample(1, mv); assert(s.shutdownPercent() == -1);
    }
    s = sample(0, 3800); assert(!s.valid());
    s = sample(0, 3250); assert(s.valid() && s.shutdownPercent() == 0);
    auto limited = low;
    limited[0] = limited[6] = 0x28;
    limited[1] = limited[7] = 0x2A;
    s = decode(limited);
    assert(s.usb && s.charging() && !s.usbAbsent() && s.shutdownPercent() == -1);
    limited[0] = limited[6] = 8;
    assert(!decode(limited).usbAbsent());

    // Voltage rise after plugging in cannot become a capacity rise; a bad
    // initial gauge never locks the session into a voltage fallback.
    {
        Estimator e; uint32_t now = 0;
        assert(e.update(sample(-1, 0), now) == -1);
        assert(advance(e, sample(0, 3515), now, 10000) == -1);
        assert(advance(e, sample(17, 3515), now, 10000) == 17);
        assert(advance(e, sample(17, 3746, true, 2), now, 10000) == 17);
        assert(advance(e, sample(17, 3990, true, 2), now, 20000) == 17);
    }
    // Normal plug/unplug keeps the last confirmed display for the entire
    // settling window. It does not prematurely accept even a nearby new value.
    for (bool initiallyUSB : {false, true}) {
        Estimator e; uint32_t now = 0;
        seed(e, sample(50, 3800, initiallyUSB, initiallyUSB ? 2 : 5), now);
        const auto changed = sample(52, 3820, !initiallyUSB, initiallyUSB ? 5 : 2);
        assert(advance(e, changed, now, 2000) == 50);
        assert(advance(e, changed, now, 6000) == 50);
        assert(advance(e, changed, now, 2000) == 50); // eight-second boundary
        assert(advance(e, changed, now, 2000) == 51); // normal ten-second step
        assert(advance(e, changed, now, 10000) == 52);
    }
    // An error or a conflicting value cancels the hold immediately. Power
    // changes cannot resurrect a number that was already considered unknown.
    {
        Estimator e; uint32_t now = 0; seed(e, sample(50), now);
        assert(advance(e, sample(51, 3800, true, 2), now, 2000) == 50);
        assert(advance(e, sample(-1, 0, true, 2), now, 2000) == -1);
        assert(advance(e, sample(50), now, 2000) == -1);
        assert(advance(e, sample(50), now, 8000) == 50);
        assert(advance(e, sample(51, 3800, true, 2), now, 2000) == 50);
        assert(advance(e, sample(80, 3800, true, 2), now, 2000) == -1);
        assert(advance(e, sample(50), now, 2000) == -1);
        assert(advance(e, sample(50), now, 8000) == 50);
        now += 60000;
        assert(e.update(sample(50, 3800, true, 2), now) == -1);
    }
    // A presentation hold never substitutes its percentage for cutoff evidence.
    {
        Estimator e; policy::CriticalBattery cutoff; uint32_t now = 0;
        seed(e, sample(3, 3300, true, 2), now);
        s = sample(1, 3300);
        assert(advance(e, s, now, 5000, 5000) == 3);
        assert(!cutoff.update(s.shutdownPercent(), s.usbAbsent()));
        assert(advance(e, s, now, 5000, 5000) == 3);
        assert(cutoff.update(s.shutdownPercent(), s.usbAbsent()));
    }
    // USB transitions also preserve the value across millis() rollover.
    {
        Estimator e; uint32_t now = UINT32_MAX - 10000;
        seed(e, sample(17, 3515), now);
        assert(advance(e, sample(17, 3746, true, 2), now, 10000) == 17);
    }
    // The reported 17 -> 18 -> 51 path: USB/wake/read frequency cannot
    // immediately accept a new 51% estimate or count repeated cache hits.
    {
        Estimator e; uint32_t now = 0; seed(e, sample(17, 3515), now);
        now += 2000;
        assert(e.update(sample(51, 3746, true, 2), now) == -1);
        for (int repeat = 0; repeat < 1000; ++repeat)
            assert(e.update(sample(51, 3746, true, 2), now) == -1);
        assert(advance(e, sample(51, 3746, true, 2), now, 28000) == -1);
        assert(advance(e, sample(51, 3746, true, 2), now, 2000) == 51);
    }
    // Opposing outliers cannot confirm each other; recovery uses hardware %.
    {
        Estimator e; uint32_t now = 0; seed(e, sample(50), now);
        for (int raw : {80, 20, 85, 19, 84, 20})
            assert(advance(e, sample(raw), now, 2000) == -1);
        assert(advance(e, sample(50), now, 2000) == 50);
        for (int repeat = 0; repeat < 4; ++repeat)
            assert(advance(e, sample(-1, 0), now, 2000) == -1);
        assert(advance(e, sample(50), now, 10000) == 50);
    }
    // A consistent downward correction while charging cannot get stuck.
    {
        Estimator e; uint32_t now = 0; seed(e, sample(55, 3900, true, 2), now);
        assert(advance(e, sample(52, 3900, true, 2), now, 40000) == 52);
    }
    // Charge done requires USB and time; it commits 100 to stored state.
    {
        Estimator e; uint32_t now = 0; seed(e, sample(90, 4160, true, 3), now);
        assert(advance(e, sample(90, 4200, true, 4), now, 2000) == 90);
        assert(!e.full());
        assert(advance(e, sample(90, 4200, true, 4), now, 8000) == 100);
        assert(e.full());
        // Unplugging to a near-full gauge keeps the committed value, rather
        // than revealing the old 90% behind a display-only 100% override.
        assert(advance(e, sample(99, 4160), now, 2000) == 100);
        assert(!e.full());
        assert(advance(e, sample(99, 4160), now, 8000) == 100);
        assert(advance(e, sample(99, 4160), now, 2000) == 99);
    }
    {
        Estimator e; uint32_t now = 0; seed(e, sample(90, 4160), now);
        assert(advance(e, sample(90, 4160, false, 4), now, 12000) == 90);
        assert(!e.full()); // stale DONE without USB cannot manufacture full
        assert(advance(e, sample(100, 4200, true, 3), now, 32000) == 99);
        assert(!e.full()); // early gauge 100 while still charging is not done
    }
    // Invalid/absent samples and gaps interrupt settling/completion evidence.
    {
        Estimator e; uint32_t now = 0; seed(e, sample(50), now);
        assert(advance(e, sample(80), now, 20000) == -1);
        now += 60000;
        assert(e.update(sample(80), now) == -1);
        assert(advance(e, sample(80), now, 30000) == 80);
        s = sample(80); s.present = false;
        assert(advance(e, s, now, 2000) == -1);
        assert(advance(e, sample(30), now, 10000) == 30); // replacement cell
        assert(advance(e, sample(30, 4200, true, 4), now, 8000) == 30);
        assert(advance(e, sample(-1, 0, true, 4), now, 2000) == -1);
        assert(!e.full());
        assert(advance(e, sample(30, 4200, true, 4), now, 2000) == -1);
        assert(!e.full());
    }
    // Critical protection uses raw validated evidence even while the display
    // is still validating a large change. A failure resets the streak.
    {
        Estimator e; policy::CriticalBattery cutoff; uint32_t now = 0;
        seed(e, sample(17, 3515), now);
        s = sample(1, 3300);
        assert(advance(e, s, now, 5000, 5000) == -1);
        assert(!cutoff.update(s.shutdownPercent(), s.usbAbsent()));
        auto invalid = sample(-1, 0);
        assert(!cutoff.update(invalid.shutdownPercent(), invalid.usbAbsent()));
        assert(!cutoff.update(s.shutdownPercent(), s.usbAbsent()));
        assert(cutoff.update(s.shutdownPercent(), s.usbAbsent()));
        s = sample(1, 3300, true, 2);
        assert(!cutoff.update(s.shutdownPercent(), s.usbAbsent()));
    }
    // Millisecond rollover and background (5s) sampling remain well-defined.
    {
        Estimator e; uint32_t now = UINT32_MAX - 4000;
        seed(e, sample(17, 3515), now);
        assert(advance(e, sample(18, 3520), now, 20000, 5000) == 18);
    }
    std::puts("Battery tests passed: checked reads, USB, invalid input, settling, correction, full charge, cutoff and rollover.");
}

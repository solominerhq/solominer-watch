#pragma once
#include <stdint.h>

namespace battery {
// Voltage is only a plausibility check. It is never converted into capacity:
// charging and mining load both change terminal voltage without adding charge.
struct Sample {
    bool statusValid = false;
    bool present = false;
    bool usb = false;
    bool inputLimited = false;
    int direction = -1; // AXP2101: 0 standby, 1 charging, 2 discharging
    int charge = -1;    // AXP2101: 0..3 charging, 4 done, 5 stopped
    int millivolts = -1;
    int gauge = -1;

    bool charging() const { return statusValid && present && usb && direction == 1; }
    bool usbAbsent() const {
        return statusValid && !usb && !inputLimited && direction != 1;
    }
    bool valid() const {
        return statusValid && present && gauge >= 0 && gauge <= 100 &&
               millivolts >= 2000 && millivolts <= 4500 &&
               !(gauge <= 2 && millivolts >= 3500);
    }
    bool done() const {
        return valid() && usb && !inputLimited && charge == 4 && direction != 2;
    }
    int shutdownPercent() const { return valid() && usbAbsent() ? gauge : -1; }
};

// Caller serializes this state together with its cache and register reads.
// All windows use elapsed time, so screen redraw frequency cannot speed them up.
class Estimator {
    static constexpr uint32_t SETTLE_MS = 8000;
    static constexpr uint32_t CORRECTION_MS = 30000;
    static constexpr uint32_t STEP_MS = 10000;
    static constexpr uint32_t MAX_SAMPLE_GAP_MS = 6500;
    int estimate_ = -1;
    int candidate_ = -1;
    int displayed_ = -1, settleDisplay_ = -1;
    uint32_t candidateSince_ = 0, settleSince_ = 0, stepSince_ = 0;
    uint32_t lastSample_ = 0, doneSince_ = 0;
    bool haveSample_ = false, havePower_ = false, usb_ = false;
    bool charging_ = false, donePending_ = false, full_ = false;

    static int distance(int a, int b) { return a > b ? a - b : b - a; }
    static bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
        return uint32_t(now - since) >= duration;
    }
    void invalidate(uint32_t now) {
        candidate_ = -1;
        displayed_ = settleDisplay_ = -1;
        donePending_ = full_ = false;
        settleSince_ = stepSince_ = now;
    }
    int updateEstimate(const Sample& s, uint32_t now) {
        if (haveSample_ && uint32_t(now - lastSample_) > MAX_SAMPLE_GAP_MS)
            invalidate(now); // a long gap is not evidence of stable readings
        haveSample_ = true;
        lastSample_ = now;

        if (!s.statusValid) { invalidate(now); return -1; }
        if (!s.present) {
            estimate_ = -1;
            havePower_ = false;
            invalidate(now);
            return -1;
        }
        if (!havePower_ || usb_ != s.usb || charging_ != s.charging()) {
            const int previousDisplay = displayed_;
            havePower_ = true;
            usb_ = s.usb;
            charging_ = s.charging();
            invalidate(now);
            // Carry the last displayed value only across an ordinary power
            // transition, never across an error, startup or a sampling gap.
            settleDisplay_ = previousDisplay;
        }
        if (!s.valid()) { invalidate(now); return -1; }

        if (candidate_ < 0 || distance(s.gauge, candidate_) > 1) {
            candidate_ = s.gauge;
            candidateSince_ = now;
        }
        if (s.done()) {
            if (!donePending_) { donePending_ = true; doneSince_ = now; }
        } else {
            donePending_ = full_ = false;
        }

        const int target = s.usb && s.gauge == 100 ? 99 : s.gauge;
        if (!elapsed(now, settleSince_, SETTLE_MS)) {
            // Keep the existing number while valid nearby readings settle.
            // A conflicting reading cancels this presentation-only hold.
            if (settleDisplay_ >= 0 && distance(target, settleDisplay_) < 5)
                return settleDisplay_;
            settleDisplay_ = -1;
            return -1;
        }
        // Confirm completion over time and commit it to the estimate, rather
        // than displaying 100 over an unrelated internal value (e.g. 90).
        if (donePending_ && elapsed(now, doneSince_, SETTLE_MS)) {
            estimate_ = 100;
            stepSince_ = now;
            full_ = true;
            return estimate_;
        }

        if (estimate_ < 0) {
            if (!elapsed(now, candidateSince_, SETTLE_MS)) return -1;
            estimate_ = target;
            stepSince_ = now;
        } else if (distance(target, estimate_) >= 5) {
            // Do not label a large new reading as trustworthy merely because
            // three unrelated readings disagreed with the old one. While the
            // gauge recalibrates, show unknown instead of invented precision.
            if (!elapsed(now, candidateSince_, CORRECTION_MS)) return -1;
            estimate_ = target;
            stepSince_ = now;
        } else if (target != estimate_ && elapsed(now, candidateSince_, 4000) &&
                   elapsed(now, stepSince_, STEP_MS)) {
            estimate_ += target > estimate_ ? 1 : -1;
            stepSince_ = now;
        }
        // Small validated corrections are allowed in BOTH directions, including
        // while charging. An overestimate must not remain stuck forever.
        if (s.usb && !full_ && estimate_ > 99) estimate_ = 99;
        return estimate_;
    }
public:
    bool full() const { return full_; }
    int update(const Sample& s, uint32_t now) {
        displayed_ = updateEstimate(s, now);
        return displayed_;
    }
};
} // namespace battery

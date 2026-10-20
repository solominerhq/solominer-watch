#pragma once
#include <stdint.h>
namespace policy {
// One active mining profile on battery and USB. Saver controls inactivity only.
constexpr uint32_t MINING_CPU_MHZ = 80;
constexpr uint32_t MINING_GAP_MS = 16;
constexpr uint32_t SAVER_IDLE_MS = 180000;
inline bool saverIdle(bool enabled, bool usb, uint32_t now, uint32_t activity) {
  return enabled && !usb && uint32_t(now-activity)>=SAVER_IDLE_MS;
}
inline bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}
class UsbDebounce {
  bool current_ = false, candidate_ = false;
  uint32_t since_ = 0;
public:
  void reset(bool usb, uint32_t now) { current_ = candidate_ = usb; since_ = now; }
  bool update(bool usb, uint32_t now) {
    if (usb != candidate_) { candidate_ = usb; since_ = now; }
    if (current_ != candidate_ && elapsed(now, since_, 2000)) {
      current_ = candidate_; return true;
    }
    return false;
  }
  bool applied() const { return current_; }
};
class CriticalBattery {
  uint8_t streak_ = 0;
public:
  bool update(int percent, bool usbDefinitelyAbsent) {
    if (percent < 0 || percent > 2 || !usbDefinitelyAbsent) { streak_ = 0; return false; }
    if (streak_ < 2) ++streak_;
    return streak_ >= 2;
  }
};
}

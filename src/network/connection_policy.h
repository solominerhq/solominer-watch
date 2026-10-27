#pragma once
#include <stdint.h>
#include "device/runtime_policy.h"
namespace network {
class RetrySchedule {
  uint32_t last_ = 0, interval_ = 0;
  uint8_t attempt_ = 0;
public:
  void reset() { last_ = interval_ = 0; attempt_ = 0; }
  bool due(uint32_t now) const { return !interval_ || policy::elapsed(now, last_, interval_); }
  void tried(uint32_t now) {
    last_ = now; interval_ = attempt_ < 4 ? (3000u << attempt_) : 30000u;
    if (attempt_ < 4) ++attempt_;
  }
};
// Wi-Fi only: keep pool reconnect timing independent of hotspot absence.
class WifiRetrySchedule {
  RetrySchedule initial_;
  uint32_t started_ = 0, last_ = 0;
  bool active_ = false;
public:
  void reset() { initial_.reset(); active_ = false; }
  // Presentation only. Retry attempts retain the original outage start.
  bool needsAttention(uint32_t now) const {
    return active_ && policy::elapsed(now, started_, 30000);
  }
  bool due(uint32_t now, bool immediate = false) const {
    // The connection owner permits an immediate retry only while waiting,
    // or when starting a stopped radio. Do not drop a wake on a time guard.
    if (!active_ || immediate) return true;
    return policy::elapsed(now, started_, 300000)
        ? policy::elapsed(now, last_, 60000) : initial_.due(now);
  }
  void tried(uint32_t now) {
    if (!active_) { started_ = now; active_ = true; }
    last_ = now;
    initial_.tried(now);
  }
};
enum class Authorization { Waiting, Accepted, Rejected };
class PoolSession {
  uint32_t id_ = 0, authStart_ = 0, lastJob_ = 0;
  Authorization auth_ = Authorization::Waiting;
  bool hasJob_ = false;
public:
  void begin(uint32_t id, uint32_t now) { id_ = id; authStart_ = lastJob_ = now; auth_ = Authorization::Waiting; hasJob_ = false; }
  uint32_t requestId() const { return id_; }
  Authorization authorization() const { return auth_; }
  void reply(bool accepted) { auth_ = accepted ? Authorization::Accepted : Authorization::Rejected; }
  bool authorized() const { return auth_ == Authorization::Accepted; }
  bool authExpired(uint32_t now) const { return auth_ == Authorization::Waiting && policy::elapsed(now, authStart_, 15000); }
  void receivedJob(uint32_t now) { if (authorized()) { lastJob_ = now; hasJob_ = true; } }
  bool stale(uint32_t now) const { return authorized() && policy::elapsed(now, hasJob_ ? lastJob_ : authStart_, 180000); }
  bool ready() const { return authorized() && hasJob_; }
};
}

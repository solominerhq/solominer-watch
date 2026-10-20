#pragma once
#include "network/pool_status.h"
#include "network/wifi_status.h"
namespace ui {
enum class StatusAction { None, RetryWifi, OpenSetup };
enum class StatusTone { Progress, Wifi, Attention, Good, Neutral };
struct MiningStatus {
  const char* clock;
  const char* title;
  const char* detail;
  StatusAction action;
  StatusTone tone;
};
inline MiningStatus connectionStatus(bool wifi, int phase, bool cooling, bool rateReady = true, unsigned retries = 0, bool saverIdle = false, network::WifiStage wifiStage = network::WifiStage::Waiting, bool wifiAttention = false, network::WifiProblem wifiProblem = network::WifiProblem::Unknown) {
  if (saverIdle) return {"Battery Saver", "Battery Saver", "", StatusAction::None, StatusTone::Neutral};
  if (!wifi) return {"Connecting to Wi-Fi", "Connecting to Wi-Fi",
    wifiAttention ? network::problemHint(wifiProblem) : "",
    wifiStage == network::WifiStage::Waiting ? StatusAction::RetryWifi : StatusAction::None,
    StatusTone::Wifi};
  if (phase == POOL_PHASE_AUTH_REJECTED) return {"Check pool settings", "Check pool settings", "Review login in Settings", StatusAction::OpenSetup, StatusTone::Progress};
  // One label through the handshake and ordinary retries; repeated failures
  // retain guidance while another TCP/subscribe/authorize attempt is underway.
  if (retries >= 4 && phase != POOL_PHASE_MINING && phase != POOL_PHASE_WAITING_JOB)
    return {"Pool unavailable", "Pool unavailable", "Retrying automatically", StatusAction::None, StatusTone::Progress};
  if (phase == POOL_PHASE_WAITING_JOB) return {"Waiting for work", "Waiting for work", "", StatusAction::None, StatusTone::Progress};
  if (phase != POOL_PHASE_MINING) return {"Joining pool", "Joining pool", "", StatusAction::None, StatusTone::Progress};
  if (cooling) return {"Cooling down", "Cooling down", "Resumes automatically", StatusAction::None, StatusTone::Attention};
  if (!rateReady) return {"Starting mining", "Starting mining", "", StatusAction::None, StatusTone::Progress};
  return {"Mining", "Mining", "", StatusAction::None, StatusTone::Good};
}
inline const char* miningStatus(bool wifi, int phase, bool cooling) { return connectionStatus(wifi, phase, cooling).title; }
inline bool showHashrate(bool wifi, int phase, bool cooling) { return wifi && phase == POOL_PHASE_MINING && !cooling; }
}

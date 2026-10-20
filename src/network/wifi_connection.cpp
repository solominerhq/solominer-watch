#include "diagnostics/diagnostics.h"
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "wifi_connection.h"
#include "device/device_power.h"
#include "connection_policy.h"
#include "idle_pause.h"
#include <esp_wifi.h>
#include "wManager.h"
namespace network {
static IdlePause idlePause;
static RetrySchedule stopRetry;
static std::atomic<WifiStage> stage{WifiStage::Waiting};
static std::atomic<bool> wifiAttention{false};
WifiStage wifiStage(){return stage.load();}
bool wifiNeedsAttention(){return wifiAttention.load();}
static void reconnect(){
  stage=WifiStage::Connecting;
  if(!WiFi.reconnect())stage=WifiStage::Waiting;
}
static std::atomic<uint32_t> gotIpGeneration{0};
static std::atomic<uint32_t> connectionCount{0};
uint32_t successfulConnections(){ return connectionCount.load(); }
static std::atomic<int> reportedPhase{0};
static std::atomic<int> disconnectReason{0};
static std::atomic<WifiProblem> currentProblem{WifiProblem::Unknown};
WifiProblem wifiProblem(){return currentProblem.load();}
static std::atomic<bool> radioStopped{false};
static std::atomic<bool> saverPausePending{false};
static bool needsFreshLink=false;
static uint32_t radioGeneration=0;
static bool hasLink(){return !radioStopped && stage.load()==WifiStage::Connected && (!needsFreshLink || gotIpGeneration.load()!=radioGeneration) && WiFi.status()==WL_CONNECTED;}
int saverPhase(){return reportedPhase.load();}
bool radioAsleep(){return radioStopped.load();}
bool takeSaverPauseEvent(){return saverPausePending.exchange(false);}
int lastDisconnectReason(){return disconnectReason.load();}
static bool startRadio(){
  if(!radioStopped)return true;
  radioGeneration=gotIpGeneration.load();needsFreshLink=true;
  if(esp_wifi_start()!=ESP_OK){diagnostics::event(diagnostics::Event::SaverRadioError);return false;}
  radioStopped=false;return true;
}
static void stopRadio(){
  if(radioStopped)return;
  // Keep the initialized STA configuration and credentials. No NVS writes,
  // disconnect/erase flags or Wi-Fi deinitialization on the idle path.
  if(esp_wifi_stop()==ESP_OK){
    radioStopped=true;stage=WifiStage::Waiting;
    saverPausePending.store(true); // Retain the event even if input resumes Wi-Fi before the UI polls.
  }
  else diagnostics::event(diagnostics::Event::SaverRadioError);
}
void serviceIdle(){
  const uint32_t now=millis();
  wifiAttention=false; // Saver sleep is intentional, not a connection problem.
  if(idlePause.step(true)==IdleAction::Stop){
    diagnostics::event(diagnostics::Event::SaverPause);
    stopRetry.reset();
  }
  reportedPhase=1;
  // Retry a failed shutdown with backoff; never restart or reconnect while idle.
  if(!radioStopped && stopRetry.due(now)){
    stopRadio();
    if(!radioStopped)stopRetry.tried(now);
  }
}
static std::atomic<bool> reconnectRequested{false};
void requestReconnect() { reconnectRequested.store(true); }
void saveWifiCredentials(const char* ssid, const char* password) { WiFi.begin(ssid, password); }
void begin() {
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    switch(event){
      case ARDUINO_EVENT_WIFI_STA_CONNECTED: stage=WifiStage::GettingAddress;break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        wifiAttention=false;
        currentProblem=WifiProblem::Unknown;
        ++gotIpGeneration;
        if(stage.exchange(WifiStage::Connected)!=WifiStage::Connected) {
          device::noteWifiConnected();
          ++connectionCount;
        }
        break;
      case ARDUINO_EVENT_WIFI_STA_LOST_IP: stage=WifiStage::GettingAddress;break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        // reconnect() first leaves the old association. That local event is
        // not a failed attempt; the subsequent association/failure decides.
        if(info.wifi_sta_disconnected.reason!=WIFI_REASON_ASSOC_LEAVE) {
          disconnectReason=info.wifi_sta_disconnected.reason;
          currentProblem=problemForReason(info.wifi_sta_disconnected.reason);
          diagnostics::event(diagnostics::Event::WifiDisconnected);
          stage=WifiStage::Waiting;
        }
        break;
      case ARDUINO_EVENT_WIFI_STA_STOP: stage=WifiStage::Waiting;break;
      default: break;
    }
  });
  stage=WifiStage::Connecting;
  init_WifiManager();
  // Explicit retries have one owner after provisioning. WiFiManager is no
  // longer allowed to run a second automatic reconnect policy underneath it.
  WiFi.setAutoReconnect(false);
}
bool service() {
  static WifiRetrySchedule retry;
  static bool linked = false, modeApplied = false;
  static uint32_t connectedAt = 0;
  const uint32_t now = millis();
  bool returning=idlePause.step(false)==IdleAction::Resume;
  if(returning){
    currentProblem=WifiProblem::Unknown;
    reportedPhase=0;linked=false;
    modeApplied=false;retry.reset();
    diagnostics::event(diagnostics::Event::SaverResume);
  }
  wifiAttention = !hasLink() && retry.needsAttention(now);
  // Consume once, including the radio-start path. UI tasks only post requests;
  // this owner decides whether an attempt is waiting or already in progress.
  const bool wakeRequested = reconnectRequested.exchange(false);
  bool radioStarted = false;
  if(radioStopped){
    // Start failures use the existing bounded foreground backoff.
    if(!retry.due(now,wakeRequested))return false;
    if(!startRadio()){retry.tried(now);return false;}
    modeApplied=false;radioStarted=true;
  }
  if (!modeApplied) {
    modeApplied = WiFi.setSleep(WIFI_PS_MIN_MODEM);
  }
  if (!hasLink()) {
    linked = false;
    // Skip the wait after a failure, but never let a wake restart association
    // or DHCP. Automatic retry deadlines still recover a stalled attempt.
    const bool retryNow = radioStarted || (wakeRequested && stage.load()==WifiStage::Waiting);
    if (retry.due(now, retryNow)) { diagnostics::event(diagnostics::Event::WifiRetry); reconnect(); retry.tried(now); }
    return false;
  }
  needsFreshLink=false;
  retry.reset();
  if (!linked) { linked = true; connectedAt = now; }
  return policy::elapsed(now, connectedAt, 2000);
}
}

#pragma once
#include <stdint.h>
#include "wifi_status.h"
namespace network {
void saveWifiCredentials(const char* ssid, const char* password); // setup only, followed by reboot
void begin(); // setup only: owns provisioning and hands off to the Stratum task
void serviceIdle(); // Stratum only, after closing the pool and invalidating work.
uint32_t successfulConnections(); // Connection edges, not repeated IP notifications.
WifiStage wifiStage(); // Current association/address state for display.
bool radioAsleep(); // True only after esp_wifi_stop succeeded, not just a pause request.
bool wifiNeedsAttention(); // Active connection attempts have failed for 30 seconds.
bool takeSaverPauseEvent(); // UI owner only: consume one confirmed Battery Saver radio shutdown.
int saverPhase(); // 0 active, 1 intentionally paused with radio off.
int lastDisconnectReason(); // Last non-local ESP Wi-Fi reason; 0 before a failure.
bool service(); // Stratum task only: non-blocking retry schedule + link readiness
void requestReconnect(); // any task: coalesced immediate retry while waiting; never interrupts association/DHCP
}

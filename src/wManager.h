#ifndef _WMANAGER_H
#define _WMANAGER_H

#include <stdint.h>

void init_WifiManager();
void reset_configuration();
void reset_full_configuration();
void reboot_to_setup();
bool isSetupBootRequested();
bool hasStoredConfig();
void requestSetupExit();
bool consumeSetupExit();

// Read the target and attempt together; setup and display run on different tasks.
struct WifiTargetSnapshot {
    char ssid[33]; // Wi-Fi SSIDs are at most 32 bytes, plus a terminator.
    uint32_t setupAttempt;
};
WifiTargetSnapshot wifiTargetSnapshot();
extern char setupNetworkName[33]; // actual provisioning AP; fixed before portal starts

// Flags for display to track WiFi setup progress
// Set true when user submits the captive portal form (credentials being tried)
extern volatile bool wifiSetupSubmitted;
// Set true when WiFi connects successfully after first-time setup
extern volatile bool wifiSetupConnected;

#endif // _WMANAGER_H

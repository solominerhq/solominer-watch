#pragma once
#include <WiFiManager.h>
#include "drivers/storage/storage.h"

// A thin UI adapter over the pinned WiFiManager. Connection retries, credential
// persistence, captive DNS, portal timeout and OTA writes remain library-owned.
class SoloMinerWiFiManager : public WiFiManager {
public:
    void configureBrandedPortal(const TSettings& settings, std::function<bool()> saved);
    void clearBrandedPortalCallbacks();
private:
    const TSettings* portalSettings_ = nullptr;
    std::function<bool()> saved_;
    String token_;
    bool scanPending_ = false;
    void registerBrandedRoutes();
    void serveBrandedPage();
    void serveSettings();
    void serveNetworks();
    void saveBrandedSettings();
    void jsonError(int status, const char* field, const char* message);
    void noCache();
};

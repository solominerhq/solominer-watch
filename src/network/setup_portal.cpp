#include "setup_portal.h"
#include "setup_timezones.h"
#include "setup_portal_asset.h"
#include "utils.h"
#include "version.h"
#include <ArduinoJson.h>
#include <esp_system.h>

void SoloMinerWiFiManager::configureBrandedPortal(const TSettings& settings, std::function<bool()> saved) {
    portalSettings_ = &settings;
    saved_ = saved;
    char token[33];
    snprintf(token, sizeof(token), "%08x%08x%08x%08x", esp_random(), esp_random(), esp_random(), esp_random());
    token_ = token;
    scanPending_ = false;
    // WiFiManager invokes this before registering its own routes. WebServer
    // uses the first matching handler, so no library patch is necessary.
    setWebServerCallback([this] { registerBrandedRoutes(); });
}
void SoloMinerWiFiManager::clearBrandedPortalCallbacks() {
    setWebServerCallback(nullptr);
    saved_ = nullptr;
    portalSettings_ = nullptr;
    token_ = "";
}
void SoloMinerWiFiManager::noCache() {
    server->sendHeader("Cache-Control", "no-store");
    server->sendHeader("X-Content-Type-Options", "nosniff");
}
void SoloMinerWiFiManager::registerBrandedRoutes() {
    for (const char* path : {"/", "/wifi", "/0wifi", "/param", "/update"})
        server->on(path, HTTP_GET, [this] { serveBrandedPage(); });
    server->on("/api/setup", HTTP_GET, [this] { serveSettings(); });
    server->on("/api/networks", HTTP_GET, [this] { serveNetworks(); });
    for (const char* path : {"/wifisave", "/paramsave"}) {
        // Intercept all methods so a GET cannot fall through to the legacy save.
        server->on(path, [this] {
            if (server->method() != HTTP_POST) { jsonError(405, "", "Use Save & connect to save settings."); return; }
            saveBrandedSettings();
        });
    }
    // /u remains WiFiManager's firmware upload handler (including image
    // validation, error reporting and restart). Only its UI is replaced.
}
void SoloMinerWiFiManager::serveBrandedPage() {
    if (captivePortal()) return;
    handleRequest();
    noCache();
    server->sendHeader("Content-Encoding", "gzip");
    server->send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(setup_portal::htmlGzip), sizeof(setup_portal::htmlGzip));
}
void SoloMinerWiFiManager::serveSettings() {
    handleRequest(); noCache();
    if (!portalSettings_) { jsonError(503, "", "Setup is not available. Reopen Setup on your watch."); return; }
    const auto& s = *portalSettings_;
    DynamicJsonDocument doc(10000);
    doc["version"] = CURRENT_VERSION;
    doc["token"] = token_;
    doc["ssid"] = WiFi_SSID(true);
    doc["hasPassword"] = WiFi_psk(true).length() > 0;
    doc["address"] = s.BtcWallet;
    doc["timezone"] = s.PosixTZ;
    doc["clockFormat"] = s.clockFormat;
    doc["worker"] = s.WorkerName;
    doc["pool"] = s.PoolAddress;
    doc["port"] = s.PoolPort;
    doc["poolPassword"] = s.PoolPassword;
    JsonArray zones = doc.createNestedArray("zones");
    for (const auto& zone : setup_portal::timezones) {
        JsonObject z = zones.createNestedObject();
        z["label"] = zone.label; z["iana"] = zone.iana; z["value"] = zone.posix;
    }
    for (int offset=-12; offset<=14; ++offset) {
        char label[40], value[16];
        snprintf(label,sizeof(label),"UTC%+d (fixed offset)",offset);
        snprintf(value,sizeof(value),"UTC%d",-offset);
        JsonObject z=zones.createNestedObject();
        z["label"]=label; z["value"]=value; z["offset"]=offset;
    }
    if (doc.overflowed()) { jsonError(503,"","Could not load settings. Please try again."); return; }
    String json; serializeJson(doc,json); server->send(200,"application/json",json);
}
void SoloMinerWiFiManager::serveNetworks() {
    handleRequest(); noCache();
    int count=WiFi.scanComplete();
    if (server->hasArg("refresh") && !scanPending_) {
        WiFi.scanDelete(); count=WIFI_SCAN_FAILED;
    }
    if (count==WIFI_SCAN_FAILED && !scanPending_) {
        scanPending_=true;
        count=WiFi.scanNetworks(true, false);
    }
    DynamicJsonDocument doc(8192);
    JsonArray list=doc.createNestedArray("networks");
    if (count==WIFI_SCAN_RUNNING) doc["scanning"]=true;
    else if (count<0) { scanPending_=false; doc["error"]="Could not scan. Try again or enter the network manually."; }
    else {
        scanPending_=false; doc["scanning"]=false;
        // Keep the response bounded; deduplicate repeated access-point SSIDs.
        for (int i=0; i<count && list.size()<30; ++i) {
            String name=WiFi.SSID(i); if (!name.length()) continue;
            bool duplicate=false;
            for (JsonObject item:list) if (name==item["ssid"].as<String>()) { duplicate=true; break; }
            if (duplicate) continue;
            JsonObject item=list.createNestedObject();
            item["ssid"]=name; item["rssi"]=WiFi.RSSI(i); item["open"]=WiFi.encryptionType(i)==WIFI_AUTH_OPEN;
        }
    }
    if (doc.overflowed()) { jsonError(503,"","Could not scan networks. Enter the network name manually."); return; }
    String json; serializeJson(doc,json); server->send(200,"application/json",json);
}
void SoloMinerWiFiManager::jsonError(int status,const char* field,const char* message) {
    noCache();
    StaticJsonDocument<384> doc;
    doc["ok"]=false; doc["field"]=field; doc["message"]=message;
    String json; serializeJson(doc,json); server->send(status,"application/json",json);
}
void SoloMinerWiFiManager::saveBrandedSettings() {
    handleRequest();
    if (!portalSettings_ || !saved_ || !token_.length() || server->arg("token")!=token_) {
        jsonError(403,"","Setup session expired. Reload this page and try again."); return;
    }
    const String ssid=server->arg("s");
    String password=server->arg("p");
    const bool keepPassword=server->arg("keepPassword")=="1";
    if (ssid.length()==0 || ssid.length()>32) { jsonError(400,"network","Enter a Wi-Fi network name, up to 32 bytes."); return; }
    if (keepPassword) {
        if (ssid!=WiFi_SSID(true)) { jsonError(400,"password","Enter the password for this network."); return; }
        password=WiFi_psk(true);
    } else if (server->arg("open")!="1") {
        bool hex=password.length()==64;
        if (hex) for (size_t i=0;i<password.length();++i) if (!isxdigit(static_cast<unsigned char>(password[i]))) hex=false;
        if ((password.length()<8 || password.length()>63) && !hex) { jsonError(400,"password","Check the Wi-Fi password (usually 8–63 characters)."); return; }
    } else password="";
    String address=server->arg("btcAddress"); address.trim();
    if (server->arg("btcAddress").length()>=sizeof(portalSettings_->BtcWallet) || !isValidBtcAddress(address.c_str())) {
        jsonError(400,"address","That Bitcoin address is not valid. Copy it again from your wallet."); return;
    }
    const String tz=server->arg("PosixTZ");
    bool validTz=tz.length()>0 && tz.length()<sizeof(portalSettings_->PosixTZ) && (setup_portal::knownTimezone(tz.c_str()) || tz==portalSettings_->PosixTZ);
    for (int offset=-12;offset<=14;++offset) if (tz==String("UTC")+String(-offset)) validTz=true;
    if (!validTz) { jsonError(400,"timezone","Choose a timezone from the list."); return; }
    if (server->arg("ClockFormat")!="12" && server->arg("ClockFormat")!="24") { jsonError(400,"timezone","Choose a 12-hour or 24-hour clock."); return; }
    String pool=server->arg("Poolurl"); pool.trim();
    bool validPool=pool.length()>0 && pool.length()<=80;
    for (size_t i=0;i<pool.length();++i) if (static_cast<unsigned char>(pool[i])<=32 || pool[i]=='/' || pool[i]=='\\') validPool=false;
    const String port=server->arg("Poolport");
    bool validPort=port.length()>0 && port.length()<=5;
    for (size_t i=0;i<port.length();++i) if (!isdigit(static_cast<unsigned char>(port[i]))) validPort=false;
    if (!validPool || !validPort || port.toInt()<1 || port.toInt()>65535 || server->arg("WorkerName").length()>=sizeof(portalSettings_->WorkerName) || server->arg("Poolpassword").length()>=sizeof(portalSettings_->PoolPassword)) {
        jsonError(400,"advanced","Check the pool host, port (1–65535), and field lengths."); return;
    }
    // The existing callback performs an atomic flash save and rolls Settings
    // back if it fails. Do not touch credentials or queue a connection first.
    doParamSave();
    if (!saved_()) { jsonError(500,"","Could not save to the watch. Your previous settings are unchanged. Try again."); return; }
    _ssid=ssid; _pass=password;
    if (_presavewificallback) _presavewificallback();
    noCache(); server->send(200,"application/json","{\"ok\":true}");
    connect=true; // processed by WiFiManager after this HTTP response completes
}

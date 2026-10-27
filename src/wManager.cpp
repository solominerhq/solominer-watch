#include "settings/settings.h"
#include "network/portal_cleanup.h"
#include "network/portal_session.h"
#include "network/setup_portal.h"
#include <atomic>
#define ESP_DRD_USE_SPIFFS true

// Include Libraries
//#include ".h"

#include <WiFi.h>

#include <WiFiManager.h>

#include "wManager.h"
#include "seriallog.h"
#include "monitor.h"
#include "drivers/displays/display.h"
#include "drivers/storage/SDCard.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"
#include "mining.h"
#include "timeconst.h"
#include "version.h"

#include <ArduinoJson.h>
#include <esp_flash.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <SPIFFS.h>

// Publish complete snapshots so the display cannot read a partly copied name.
static WifiTargetSnapshot wifiTarget = {};
static portMUX_TYPE wifiTargetMux = portMUX_INITIALIZER_UNLOCKED;

WifiTargetSnapshot wifiTargetSnapshot() {
    portENTER_CRITICAL(&wifiTargetMux);
    const WifiTargetSnapshot result = wifiTarget;
    portEXIT_CRITICAL(&wifiTargetMux);
    return result;
}

static void setWifiTarget(const char* ssid, bool newAttempt = false) {
    char name[sizeof(wifiTarget.ssid)] = {};
    if (ssid) strncpy(name, ssid, sizeof(name) - 1);
    portENTER_CRITICAL(&wifiTargetMux);
    memcpy(wifiTarget.ssid, name, sizeof(name));
    if (newAttempt) ++wifiTarget.setupAttempt;
    portEXIT_CRITICAL(&wifiTargetMux);
}

// Flags for display to track WiFi setup progress
volatile bool wifiSetupSubmitted = false;
volatile bool wifiSetupConnected = false;

static std::atomic<bool> setupExitRequested{false};
static std::atomic<bool> setupExitCompleted{false};
void requestSetupExit() { setupExitRequested.store(true); }
bool consumeSetupExit() { return setupExitCompleted.exchange(false); }

// Flag for saving data
bool shouldSaveConfig = false;

// Trim leading/trailing whitespace (spaces, tabs, CR/LF) from a C string in place.
// Pasted Bitcoin addresses and worker names frequently carry a stray trailing
// space or newline from copy/paste, which would silently corrupt the value
// sent to the pool. This is the cheap, no-false-reject guard against that.
static void trimInPlace(char *s) {
    if (!s) return;
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') s[--len] = '\0';
        else break;
    }
}

// Copy a submitted POSIX TZ value into Settings with light validation. The
// value comes from our own dropdown, but a tampered form could send anything;
// accept only a plausible TZ string (non-empty, fits the buffer, printable),
// otherwise leave the existing setting untouched. Returns true if applied.
static bool applyPosixTzSelection(const char* val) {
    if (!val || val[0] == '\0') return false;
    size_t n = strlen(val);
    if (n >= sizeof(((TSettings*)0)->PosixTZ)) return false;
    for (size_t i = 0; i < n; i++) {
        char c = val[i];
        if (c < 0x20 || c > 0x7E) return false;  // non-printable → reject
    }
    extern TSettings Settings;
    strlcpy(Settings.PosixTZ, val, sizeof(Settings.PosixTZ));
    return true;
}

// Variables to hold data from custom textboxes
TSettings Settings;

// Define WiFiManager Object
SoloMinerWiFiManager wm;
extern monitor_data mMonitor;

nvMemory nvMem;

extern SDCard SDCrd;

// Full power-off (display driver): battery → PMU shutdown, USB → restart into
// the charging screen. Used when the setup portal times out unconfigured.
extern void powerOffDevice(void);

String readCustomAPName() {
    SERIAL_PRINTLN("DEBUG: Attempting to read custom AP name from flash at 0x3F0000...");
    
    // Leer directamente desde flash
    const size_t DATA_SIZE = 128;
    uint8_t buffer[DATA_SIZE];
    memset(buffer, 0, DATA_SIZE); // Clear buffer
    
    // Leer desde 0x3F0000
    esp_err_t result = esp_flash_read(NULL, buffer, 0x3F0000, DATA_SIZE);
    if (result != ESP_OK) {
        SERIAL_PRINTF("DEBUG: Flash read error: %s\n", esp_err_to_name(result));
        return "";
    }
    
    SERIAL_PRINTLN("DEBUG: Successfully read from flash");
    String data = String((char*)buffer);
    
    // Debug: show raw data read
    SERIAL_PRINTF("DEBUG: Raw flash data: '%s'\n", data.c_str());
    
    if (data.startsWith("WEBFLASHER_CONFIG:")) {
        SERIAL_PRINTLN("DEBUG: Found WEBFLASHER_CONFIG marker");
        String jsonPart = data.substring(18); // Después del marcador "WEBFLASHER_CONFIG:"
        
        SERIAL_PRINTF("DEBUG: JSON part: '%s'\n", jsonPart.c_str());
        
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, jsonPart);
        
        if (error == DeserializationError::Ok) {
            SERIAL_PRINTLN("DEBUG: JSON parsed successfully");
            
            if (doc.containsKey("apname")) {
                String customAP = doc["apname"].as<String>();
                customAP.trim();
                
                if (customAP.length() > 0 && customAP.length() < 32) {
                    SERIAL_PRINTF("✅ Custom AP name from webflasher: %s\n", customAP.c_str());
                    return customAP;
                } else {
                    SERIAL_PRINTF("DEBUG: AP name invalid length: %d\n", customAP.length());
                }
            } else {
                SERIAL_PRINTLN("DEBUG: 'apname' key not found in JSON");
            }
        } else {
            SERIAL_PRINTF("DEBUG: JSON parse error: %s\n", error.c_str());
        }
    } else {
        SERIAL_PRINTLN("DEBUG: WEBFLASHER_CONFIG marker not found - no custom config");
    }
    
    SERIAL_PRINTLN("DEBUG: Using default AP name");
    return "";
}

void preSaveConfigCallback()
{
    if (!wm.server) return;
    // WiFiManager calls this after parsing the form but BEFORE applying it.
    // getWiFiSSID() still returns the old STA/NVS name at this point.
    String submittedSSID = wm.server->arg("s");
    // Mirror WiFiManager's password-only update. Empty SSID and password means
    // parameters only: no new network attempt and no Connecting screen.
    if (submittedSSID.length() == 0 && wm.server->arg("p").length() > 0)
        submittedSSID = wm.getWiFiSSID(true);
    if (submittedSSID.length() == 0) return;

    setWifiTarget(submittedSSID.c_str(), true);
    wifiSetupConnected = false;
    wifiSetupSubmitted = true;
    SERIAL_PRINTLN("Setup submitted — connection target updated");
}

void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
    SERIAL_PRINTLN("Should save config");
    shouldSaveConfig = true;

    // Also called by doParamSave(), before the new Wi-Fi connection starts.
    // Preserve the target already captured from the submitted form.
}

/* void saveParamsCallback()
// Callback notifying us of the need to save configuration
{
    SERIAL_PRINTLN("Should save config");
    shouldSaveConfig = true;
    nvMem.saveConfig(&Settings);
} */

void configModeCallback(WiFiManager* myWiFiManager)
// Called when config mode launched
{
    SERIAL_PRINTLN("Entered Configuration Mode");
    // Signal the Monitor task to show the setup screen via the ScreenDispatcher.
    // Calling drawSetupScreen() directly from here would race with the Monitor task.
    mMonitor.NerdStatus = NM_waitingConfig;
    SERIAL_PRINT("Config SSID: ");
    SERIAL_PRINTLN(myWiFiManager->getConfigPortalSSID());

    SERIAL_PRINT("Config IP Address: ");
    SERIAL_PRINTLN(WiFi.softAPIP());
}

// ── SPIFFS force-setup flag ──
// Written before reboot, checked on next boot. Survives restart reliably
// (unlike NVS which can race with ESP.restart).
#define FORCE_SETUP_FLAG "/forceSetup"

static bool setForceSetupFlag() {
    File f = SPIFFS.open(FORCE_SETUP_FLAG, "w");
    if (!f) return false;
    const bool written = f.print("1") == 1;
    f.close();
    if (!written) SPIFFS.remove(FORCE_SETUP_FLAG);
    return written && SPIFFS.exists(FORCE_SETUP_FLAG);
}

bool isSetupBootRequested() {
    return SPIFFS.exists(FORCE_SETUP_FLAG);
}

static bool checkAndClearForceSetupFlag() {
    if (SPIFFS.exists(FORCE_SETUP_FLAG)) {
        SPIFFS.remove(FORCE_SETUP_FLAG);
        return true;
    }
    return false;
}

void reboot_to_setup()
{
    if (!setForceSetupFlag()) {
        SERIAL_PRINTLN("Could not save setup request; staying on the watch");
        return;
    }
    checkpointMiningStats();
    SERIAL_PRINTLN("Rebooting into setup mode (WiFi preserved)");
    delay(100);
    ESP.restart();
}

void reset_configuration()
{
    SERIAL_PRINTLN("Clearing WiFi credentials, restarting");
    setForceSetupFlag();
    wm.resetSettings();  // Clears only WiFi credentials — wallet, pool, timezone preserved
    delay(100);
    ESP.restart();
}

void reset_full_configuration()
{
    SERIAL_PRINTLN("Full factory reset — wiping all config");
    nvMem.deleteConfig();   // Wipes wallet, pool, timezone from SPIFFS
    resetStat();            // Wipes mining statistics
    wm.resetSettings();     // Wipes WiFi credentials
    ESP.restart();
}

bool hasStoredConfig()
{
    TSettings tempSettings;
    return nvMem.loadConfig(&tempSettings);
}

char setupNetworkName[33] = DEFAULT_SSID;

void init_WifiManager()
{
    setupExitRequested.store(false);
    setupExitCompleted.store(false);
#ifdef MONITOR_SPEED
    Serial.begin(MONITOR_SPEED);
#else
    Serial.begin(115200);
#endif //MONITOR_SPEED
    //Serial.setTxTimeoutMs(10);
    
    // Check for custom AP name from flasher config, otherwise use default
    String customAPName = readCustomAPName();
    snprintf(setupNetworkName, sizeof(setupNetworkName), "%s",
             customAPName.length() > 0 && customAPName.length() <= 32 ? customAPName.c_str() : DEFAULT_SSID);
    const char* apName = setupNetworkName;
    wm.setDebugOutput(SOLOMINER_VERBOSE_LOGS != 0);

    //Init pin 15 to eneble 5V external power (LilyGo bug)
#ifdef PIN_ENABLE5V
    pinMode(PIN_ENABLE5V, OUTPUT);
    digitalWrite(PIN_ENABLE5V, HIGH);
#endif

    // Change to true when testing to force configuration every time we run
    // Consume an explicit request even if configuration is missing or another
    // condition also opens setup; otherwise it can reopen on a later restart.
    bool forceConfig = checkAndClearForceSetupFlag();

#if defined(PIN_BUTTON_2)
    // Check if button2 is pressed to enter configMode with actual configuration
    if (!digitalRead(PIN_BUTTON_2)) {
        SERIAL_PRINTLN(F("Button pressed to force start config mode"));
        forceConfig = true;
        wm.setBreakAfterConfig(true); //Set to detect config edition and save
    }
#endif
    // Explicitly set WiFi mode
    WiFi.mode(WIFI_STA);

    // Reduce TX power — watch is typically within a few feet of router/phone.
    // 8.5dBm (~7mW) is below industry standard (~10-13dBm) but proven reliable.
    // If WiFi connectivity becomes unreliable at distance, try:
    //   WiFi.setTxPower(WIFI_POWER_13dBm);     // ~20mW, ~10m range (industry standard)
    //   WiFi.setTxPower(WIFI_POWER_19_5dBm);   // ~90mW, full range (ESP32 default)
    // To save more battery at the cost of range:
    //   WiFi.setTxPower(WIFI_POWER_5dBm);      // ~3mW, saves 2-4mA, ~3m range
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // Cache the stored WiFi SSID from ESP32 NVS so the display can show it
    // during connection (WiFi.SSID() is only populated after a successful connection)
    setWifiTarget("");
    wifi_config_t _wifiConf;
    memset(&_wifiConf, 0, sizeof(_wifiConf));
    if (esp_wifi_get_config(WIFI_IF_STA, &_wifiConf) == ESP_OK && _wifiConf.sta.ssid[0] != '\0') {
        char storedSSID[33] = {};
        memcpy(storedSSID, _wifiConf.sta.ssid, sizeof(_wifiConf.sta.ssid));
        setWifiTarget(storedSSID);
        SERIAL_PRINTF("Cached WiFi target SSID: %s\n", storedSSID);
    }

    if (!nvMem.loadConfig(&Settings))
    {
        //No config file on internal flash.
        if (SDCrd.loadConfigFile(&Settings))
        {
            //Config file on SD card.
            SDCrd.SD2nvMemory(&nvMem, &Settings); // reboot on success.
        }
        else
        {
            //No config file on SD card. Starting wifi config server.
            forceConfig = true;
        }
    };

    // Keep an unconfigured watch reachable even without stored Wi-Fi.
    if (!forceConfig && wifiTargetSnapshot().ssid[0] == '\0') {
        SERIAL_PRINTLN("[WIFI] No stored WiFi credentials — forcing config portal");
        forceConfig = true;
    }

    // Free the memory from SDCard class 
    SDCrd.terminate();
    
    // Reset settings (only for development)
    //wm.resetSettings();

    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setPreSaveConfigCallback(preSaveConfigCallback);
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalBlocking(false);
    wm.setConnectTimeout(40);
    wm.setConfigPortalTimeout(600);

    // These parameters keep the existing durable-save path. Their presentation
    // is provided by the branded, self-contained phone portal.
    WiFiManagerParameter addr_text_box("btcAddress", "Bitcoin address", Settings.BtcWallet, 79);
    WiFiManagerParameter worker_text_box("WorkerName", "Device name", Settings.WorkerName, 31);
    WiFiManagerParameter pool_text_box("Poolurl", "Pool host", Settings.PoolAddress.c_str(), 80);
    char convertedValue[6];
    snprintf(convertedValue, sizeof(convertedValue), "%d", Settings.PoolPort);
    WiFiManagerParameter port_text_box_num("Poolport", "Pool port", convertedValue, 5);
    WiFiManagerParameter password_text_box("Poolpassword", "Pool password", Settings.PoolPassword, 79);
    WiFiManagerParameter tz_select("PosixTZ", "Timezone", Settings.PosixTZ, 47);
    char clockValue[3];
    snprintf(clockValue, sizeof(clockValue), "%d", Settings.clockFormat);
    WiFiManagerParameter clock_select("ClockFormat", "Clock format", clockValue, 2);
    wm.addParameter(&addr_text_box);
    wm.addParameter(&worker_text_box);
    wm.addParameter(&pool_text_box);
    wm.addParameter(&port_text_box_num);
    wm.addParameter(&password_text_box);
    wm.addParameter(&tz_select);
    wm.addParameter(&clock_select);
  #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
  char checkboxParams2[24] = "type=\"checkbox\"";
  if (Settings.invertColors)
  {
    strcat(checkboxParams2, " checked");
  }
  WiFiManagerParameter invertColors("inverColors", "Invert Display Colors (if the colors looks weird)", "T", 2, checkboxParams2, WFM_LABEL_AFTER);
  wm.addParameter(&invertColors);
  #endif
  #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
    char brightnessConvValue[4];
    snprintf(brightnessConvValue, sizeof(brightnessConvValue), "%d", Settings.Brightness);
    // Text box (Number) - 3 characters maximum
    WiFiManagerParameter brightness_text_box_num("Brightness", "Screen backlight Duty Cycle (0-255)", brightnessConvValue, 3);
    wm.addParameter(&brightness_text_box_num);
  #endif

    bool portalConfigSaved = false;
    // WiFiManager calls this after reading form values, BEFORE attempting Wi-Fi.
    // A parameters-only Save leaves its portal running; persist here rather than
    // waiting for startConfigPortal() to return (which may never happen before
    // the user restarts). A failed Wi-Fi attempt must not lose these settings.
    auto savePortalParameters = [&]() {
        saveConfigCallback();
        const TSettings previous = Settings;
        Settings.PoolAddress = pool_text_box.getValue();
        Settings.PoolPort = atoi(port_text_box_num.getValue());
        strlcpy(Settings.PoolPassword, password_text_box.getValue(), sizeof(Settings.PoolPassword));
        strlcpy(Settings.BtcWallet, addr_text_box.getValue(), sizeof(Settings.BtcWallet));
        strlcpy(Settings.WorkerName, worker_text_box.getValue(), sizeof(Settings.WorkerName));
        applyPosixTzSelection(tz_select.getValue());
        const int clockFormat = atoi(clock_select.getValue());
        if (clockFormat == 12 || clockFormat == 24) Settings.clockFormat = clockFormat;
        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
        Settings.invertColors = (strncmp(invertColors.getValue(), "T", 1) == 0);
        Settings.Brightness = atoi(brightness_text_box_num.getValue());
        #endif
        settings::normalize(Settings);
        portalConfigSaved = nvMem.saveConfig(&Settings);
        if (portalConfigSaved) {
            applyTimezone();
        } else {
            // Keep the last persisted settings if the flash write fails.
            // saveConfig already records SettingsSaveFailed in diagnostics.
            Settings = previous;
        }
    };
    wm.setSaveParamsCallback(savePortalParameters);

    SERIAL_PRINTLN("AllDone: ");
    wm.configureBrandedPortal(Settings, [&]() { return portalConfigSaved; });
    if (forceConfig)
    {
        // Keep boot here, but own the processing loop so watch input can request
        // a safe exit without touching WiFiManager from the display task.
        wm.setConfigPortalBlocking(false);
        // Keep the portal alive while a phone is connected to the setup AP —
        // a user slowly filling in the form must never be cut off mid-setup.
        // With this, the 10-min timeout can only fire when setup is truly
        // abandoned: no phone on the AP and no portal page activity.
        wm.setAPClientCheck(true);
        // NerdStatus remains NM_waitingConfig (set before Monitor task started) so
        // the Monitor task will display the setup screen automatically
        wm.startConfigPortal(apName, DEFAULT_WIFIPW);
        const bool exitedByUser = network::runProvisioningPortal(wm,
            [] { return setupExitRequested.exchange(false); }, [] { delay(10); });

        // Portal closed with nothing configured (timeout, or a rogue return).
        // Two very different situations:
        //  - Stored WiFi exists (setup entered accidentally, or user browsed the
        //    portal and walked away): resume normal boot — the mining loop
        //    reconnects to the saved network. Powering off here would kill a
        //    perfectly configured device.
        //  - No stored WiFi (first boot / after credential reset): the AP and
        //    web server are gone but the watch would keep showing a dead
        //    "Scan to Setup" QR until a power cycle. Power off (battery) or
        //    park on the charging screen (USB); PWR boots into a fresh portal.
        if (exitedByUser) {
            // Explicit exit keeps stored settings, including any completed Save.
            // First-time users may return to Clock and reopen setup when ready.
            mMonitor.NerdStatus = NM_Connecting;
            wifiSetupSubmitted = false;
        } else if (!shouldSaveConfig && WiFi.status() != WL_CONNECTED) {
            if (wifiTargetSnapshot().ssid[0] != '\0') {
                SERIAL_PRINTLN("[WIFI] Portal closed unconfigured — resuming with stored WiFi");
                mMonitor.NerdStatus = NM_Connecting;  // leave the setup screen
            } else {
                SERIAL_PRINTLN("[WIFI] Config portal timed out unconfigured — powering off");
                delay(100);  // let the log line flush
                powerOffDevice();
            }
        }

        if (shouldSaveConfig && portalConfigSaved && !exitedByUser)
        {
            // The form was persisted by the save callback, before connection.
            delay(3*SECOND_MS);
            ESP.restart();
        }
    }
    else
    {
        // Normal boot with saved credentials — try to connect to saved WiFi.
        // IMPORTANT: Do NOT open config portal if WiFi fails. The stratum worker
        // in network/wifi_connection.cpp handles reconnection with backoff.
        // The config portal should only open on explicit user request (button press
        // or reset_configuration), not on a temporary hotspot dropout.
        mMonitor.NerdStatus = NM_Connecting;
        wm.setCaptivePortalEnable(false);
        wm.setConfigPortalBlocking(false);
        wm.setEnableConfigPortal(false);   // Do NOT fall into AP mode on failure
        wm.autoConnect(apName, DEFAULT_WIFIPW);

        // If WiFi didn't connect immediately, that's OK — the mining loop will
        // keep retrying via WiFi.reconnect(). We proceed with boot either way.
        if (WiFi.status() != WL_CONNECTED) {
            SERIAL_PRINTLN("[WIFI] Initial connection failed - mining loop will retry");
        }
    }
    
    //Conectado a la red Wifi
    if (WiFi.status() == WL_CONNECTED) {
        //tft.pushImage(0, 0, MinerWidth, MinerHeight, MinerScreen);
        SERIAL_PRINTLN("");
        SERIAL_PRINTLN("WiFi connected");
        SERIAL_PRINT("IP address: ");
        SERIAL_PRINTLN(WiFi.localIP());

        // Leave provisioning as soon as association succeeds; the clock shows live pool progress.
        if (wifiSetupSubmitted) {
            mMonitor.NerdStatus = NM_Connecting;
            wifiSetupConnected = true;
            SERIAL_PRINTLN("WiFi setup complete — showing live connection progress");
        }

    }

    network::stopProvisioningPortals(wm);
    // Stop request processing before discarding the captured local parameters.
    wm.setSaveParamsCallback(nullptr);
    wm.clearBrandedPortalCallbacks();
    if (forceConfig) {
        mMonitor.NerdStatus = NM_Connecting;
        setupExitCompleted.store(true);
    }

}

#include "diagnostics/diagnostics.h"
#include "seriallog.h"
#include "settings/config_codec.h"
#include "nvMemory.h"
#include <mutex>
static std::mutex configFileMutex;

#ifdef NVMEM_SPIFFS

#include <SPIFFS.h>
#include <FS.h>
#include <ArduinoJson.h>

#include "../devices/device.h"
#include "storage.h"

nvMemory::nvMemory() : Initialized_(false){};

nvMemory::~nvMemory()
{
    if (Initialized_)
        SPIFFS.end();
};

/// @brief Save settings to config file on SPIFFS
/// @param TSettings* Settings to be saved.
/// @return true on success
bool nvMemory::saveConfig(TSettings* Settings)
{
    std::lock_guard<std::mutex> lock(configFileMutex);
    settings::normalize(*Settings);
    if (init())
    {
        // Save Config in JSON format
        SERIAL_PRINTLN(F("SPIFS: Saving configuration."));

        // Create a JSON document. 1024, not 512: a max-length wallet (80) +
        // pool password (80) + long pool URL + worker name overflow a 512
        // pool, and ArduinoJson drops the keys assigned LAST silently.
        StaticJsonDocument<1024> json;
        settings::writeConfig(json.to<JsonObject>(), *Settings);

        // Refuse to persist a silently-truncated config.
        if (json.overflowed())
        {
            SERIAL_PRINTLN(F("SPIFS: Config JSON overflowed - NOT saving truncated config"));
            diagnostics::event(diagnostics::Event::SettingsSaveFailed);
            return false;
        }

        // POWER-LOSS-SAFE WRITE: never truncate the only copy in place.
        // Write the new config to a temp file first, then atomically rename it
        // over the old one. A power cut mid-write leaves the previous config
        // intact instead of wiping the customer's wallet/pool setup.
        const char* tmpPath = JSON_CONFIG_FILE_TMP;
        File configFile = SPIFFS.open(tmpPath, "w");
        if (!configFile)
        {
            // Error, file did not open
            SERIAL_PRINTLN("SPIFS: Failed to open temp config file for writing");
            diagnostics::event(diagnostics::Event::SettingsSaveFailed);
            return false;
        }

        // Serialize JSON data to write to file.
        // (Do NOT dump the config to Serial here: it contains the pool
        // password and wallet — anyone on USB would see them.)
        size_t written = serializeJson(json, configFile);
        configFile.close();
        if (written != measureJson(json))
        {
            // Error writing file — remove the bad temp, keep the old config.
            SERIAL_PRINTLN(F("SPIFS: Failed to write to file"));
            SPIFFS.remove(tmpPath);
            diagnostics::event(diagnostics::Event::SettingsSaveFailed);
            return false;
        }

        // Atomic swap: SPIFFS.rename replaces the destination in one step.
        SPIFFS.remove(JSON_CONFIG_FILE);  // SPIFFS rename fails if dest exists
        if (!SPIFFS.rename(tmpPath, JSON_CONFIG_FILE))
        {
            SERIAL_PRINTLN(F("SPIFS: Failed to swap in new config"));
            diagnostics::event(diagnostics::Event::SettingsSaveFailed);
            return false;
        }
        return true;
    };
    diagnostics::event(diagnostics::Event::SettingsSaveFailed);
    return false;
}

/// @brief Load settings from config file located in SPIFFS.
/// @param TSettings* Struct to update with new settings.
/// @return true on success
bool nvMemory::loadConfig(TSettings* Settings)
{
    std::lock_guard<std::mutex> lock(configFileMutex);
    // Uncomment if we need to format filesystem
    // SPIFFS.format();

    // Load existing configuration file
    // Read configuration from FS json

    if (init())
    {
        // Crash recovery: if a previous save was interrupted between removing
        // the old config and renaming the temp over it, the temp file holds a
        // complete, fully-written config — adopt it instead of losing setup.
        if (!SPIFFS.exists(JSON_CONFIG_FILE) && SPIFFS.exists(JSON_CONFIG_FILE_TMP))
        {
            SERIAL_PRINTLN("SPIFS: Recovering config from interrupted save");
            SPIFFS.rename(JSON_CONFIG_FILE_TMP, JSON_CONFIG_FILE);
        }

        if (SPIFFS.exists(JSON_CONFIG_FILE))
        {
            // The file exists, reading and loading
            File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
            if (configFile)
            {
                SERIAL_PRINTLN("SPIFS: Loading config file");
                StaticJsonDocument<1024> json;
                DeserializationError error = deserializeJson(json, configFile);
                configFile.close();
                // (Config intentionally not echoed to Serial: contains credentials.)
                if (!error && json.is<JsonObject>())
                {
                    settings::readConfig(json.as<JsonObjectConst>(), *Settings, settings::ConfigFormat::Internal);
                    return true;
                }
                else
                {
                    // Error loading JSON data
                    SERIAL_PRINTLN("SPIFS: Error parsing config file!");
                }
            }
            else
            {
                SERIAL_PRINTLN("SPIFS: Error opening config file!");
            }
        }
        else
        {
            SERIAL_PRINTLN("SPIFS: No config file available!");
        }
    }
    return false;
}

/// @brief Delete config file from SPIFFS
/// @return true on successs
bool nvMemory::deleteConfig()
{
    std::lock_guard<std::mutex> lock(configFileMutex);
    SERIAL_PRINTLN("SPIFS: Erasing config file..");
    return SPIFFS.remove(JSON_CONFIG_FILE); //Borramos fichero
}

/// @brief Prepare and mount SPIFFS
/// @return true on success
bool nvMemory::init()
{
    if (!Initialized_)
    {
        SERIAL_PRINTLN("SPIFS: Mounting File System...");
        // May need to make it begin(true) first time you are using SPIFFS
        Initialized_ = SPIFFS.begin(false) || SPIFFS.begin(true);
        if (Initialized_) { SERIAL_PRINTLN("SPIFS: Mounted"); } else { SERIAL_PRINTLN("SPIFS: Mounting failed."); }
    }
    else
    {
        SERIAL_PRINTLN("SPIFS: Already Mounted");
    }
    return Initialized_;
};

#else

nvMemory::nvMemory() {}
nvMemory::~nvMemory() {}
bool nvMemory::saveConfig(TSettings* Settings) { return false; }
bool nvMemory::loadConfig(TSettings* Settings) { return false; }
bool nvMemory::deleteConfig() { return false; }
bool nvMemory::init() { return false; }


#endif //NVMEM_TYPE
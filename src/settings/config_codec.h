#pragma once
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"
namespace settings {
enum class ConfigFormat { Internal, SdCard };
struct ConfigKeys {
  const char *pool, *port, *password, *wallet, *worker, *timezone, *posix,
             *stats, *invert, *brightness, *clock, *screen;
};
inline ConfigKeys keys(ConfigFormat format) {
  if (format == ConfigFormat::SdCard) return {
    JSON_KEY_POOLURL, JSON_KEY_POOLPORT, JSON_KEY_POOLPASS, JSON_KEY_WALLETID,
    JSON_KEY_WORKERNAME, JSON_KEY_TIMEZONE, JSON_KEY_POSIXTZ, JSON_KEY_STATS2NV,
    JSON_KEY_INVCOLOR, JSON_KEY_BRIGHTNESS, JSON_KEY_CLOCKFORMAT, JSON_KEY_SCREENBRIGHTNESS};
  return {JSON_SPIFFS_KEY_POOLURL, JSON_SPIFFS_KEY_POOLPORT, JSON_SPIFFS_KEY_POOLPASS,
    JSON_SPIFFS_KEY_WALLETID, JSON_SPIFFS_KEY_WORKERNAME, JSON_SPIFFS_KEY_TIMEZONE,
    JSON_SPIFFS_KEY_POSIXTZ, JSON_SPIFFS_KEY_STATS2NV, JSON_SPIFFS_KEY_INVCOLOR,
    JSON_SPIFFS_KEY_BRIGHTNESS, JSON_SPIFFS_KEY_CLOCKFORMAT, JSON_SPIFFS_KEY_SCREENBRIGHTNESS};
}
template<size_t N> inline void copyText(JsonVariantConst from, char (&to)[N]) {
  if (from.is<const char*>()) { strncpy(to, from.as<const char*>(), N - 1); to[N - 1] = 0; }
}
inline void readConfig(JsonObjectConst json, TSettings& s, ConfigFormat format) {
  const auto k = keys(format);
  if (format == ConfigFormat::SdCard) {
    if (json[JSON_KEY_SSID].is<const char*>()) s.WifiSSID = json[JSON_KEY_SSID].as<const char*>();
    if (json[JSON_KEY_PASW].is<const char*>()) s.WifiPW = json[JSON_KEY_PASW].as<const char*>();
  }
  if (json[k.pool].is<const char*>()) s.PoolAddress = json[k.pool].as<const char*>();
  copyText(json[k.password], s.PoolPassword); copyText(json[k.wallet], s.BtcWallet);
  copyText(json[k.worker], s.WorkerName); copyText(json[k.posix], s.PosixTZ);
  if (json[k.port].is<int>()) s.PoolPort = json[k.port].as<int>();
  if (json[k.timezone].is<int>()) s.Timezone = json[k.timezone].as<int>();
  if (json[k.stats].is<bool>()) s.saveStats = json[k.stats].as<bool>();
  if (json[k.invert].is<bool>()) s.invertColors = json[k.invert].as<bool>();
  if (json[k.brightness].is<int>()) s.Brightness = json[k.brightness].as<int>();
  if (json[k.clock].is<int>()) s.clockFormat = json[k.clock].as<int>();
  if (json[k.screen].is<int>()) s.screenBrightness = json[k.screen].as<int>();
  const char* saver = format == ConfigFormat::SdCard ? "IdleBatterySaver" : "idleBatterySaver";
  if (json[saver].is<bool>()) s.batterySaver = json[saver].as<bool>();
  normalize(s);
  // An explicit old offset migrates without DST. Missing keys preserve defaults.
  if (!json[k.posix].is<const char*>() && json[k.timezone].is<int>())
    snprintf(s.PosixTZ, sizeof(s.PosixTZ), "UTC%d", -s.Timezone);
  // ultraSaver, UltraSaver and obsolete dim-timeout keys are deliberately ignored.
}
inline void writeConfig(JsonObject json, const TSettings& s) {
  const auto k = keys(ConfigFormat::Internal);
  json[k.pool] = s.PoolAddress.c_str(); json[k.port] = s.PoolPort;
  json[k.password] = s.PoolPassword; json[k.wallet] = s.BtcWallet;
  json[k.worker] = s.WorkerName; json[k.timezone] = s.Timezone;
  json[k.posix] = s.PosixTZ; json[k.stats] = s.saveStats;
  json[k.invert] = s.invertColors; json[k.brightness] = s.Brightness;
  json[k.clock] = s.clockFormat; json[k.screen] = s.screenBrightness;
  json["idleBatterySaver"] = s.batterySaver;
}
}

#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <Arduino.h>

// config files

// default settings
#ifndef HAN
#define DEFAULT_SSID		"SOLOMINER"
#else
#define DEFAULT_SSID		"SOLOMINER"
#endif
#define DEFAULT_WIFIPW		"minebitcoin"
#define DEFAULT_POOLURL		"public-pool.io"
#define DEFAULT_POOLPASS	"x"
#define DEFAULT_WALLETID	""
#define DEFAULT_POOLPORT	21496
#define DEFAULT_WORKERNAME	"SOLOMINER"
#define DEFAULT_TIMEZONE	-7  // UTC offset in hours (legacy; kept for migration)
// POSIX TZ string — encodes the base offset AND daylight-saving rules, so the
// clock follows DST automatically (unlike the legacy fixed Timezone offset).
// Empty = "not yet chosen": the setup dropdown shows a "Select your timezone"
// prompt, and the clock falls back to UTC until the user picks a zone.
#define DEFAULT_POSIXTZ		""
#define DEFAULT_SAVESTATS	true
#define DEFAULT_INVERTCOLORS	false
#define DEFAULT_BRIGHTNESS	250
#define DEFAULT_CLOCKFORMAT	12	// 12 = 12-hour (AM/PM), 24 = 24-hour
#define DEFAULT_SCREENBRIGHTNESS	150	// 0-255, applied as wake brightness
// Dim timeout is fixed in firmware (DIM_TIMEOUT_S in the display driver), not a setting.

// JSON config files
#define JSON_CONFIG_FILE	"/config.json"
// Temp path for power-loss-safe saves (write temp → atomic rename over real).
#define JSON_CONFIG_FILE_TMP	"/config.tmp"

// JSON config file SD card (for user interaction, readme.md)
#define JSON_KEY_SSID		"SSID"
#define JSON_KEY_PASW		"WifiPW"
#define JSON_KEY_POOLURL	"PoolUrl"
#define JSON_KEY_POOLPASS	"PoolPassword"
#define JSON_KEY_WALLETID	"BtcWallet"
#define JSON_KEY_POOLPORT	"PoolPort"
#define JSON_KEY_WORKERNAME	"WorkerName"
#define JSON_KEY_TIMEZONE	"Timezone"
#define JSON_KEY_POSIXTZ	"PosixTZ"
#define JSON_KEY_STATS2NV	"SaveStats"
#define JSON_KEY_INVCOLOR	"invertColors"
#define JSON_KEY_BRIGHTNESS	"Brightness"
#define JSON_KEY_CLOCKFORMAT	"ClockFormat"
#define JSON_KEY_SCREENBRIGHTNESS	"ScreenBrightness"

// JSON config file SPIFFS (different for backward compatibility with existing devices)
#define JSON_SPIFFS_KEY_POOLURL		"poolString"
#define JSON_SPIFFS_KEY_POOLPORT	"portNumber"
#define JSON_SPIFFS_KEY_POOLPASS	"poolPassword"
#define JSON_SPIFFS_KEY_WALLETID	"btcString"
#define JSON_SPIFFS_KEY_WORKERNAME	"workerName"
#define JSON_SPIFFS_KEY_TIMEZONE	"gmtZone"
#define JSON_SPIFFS_KEY_POSIXTZ		"posixTZ"
#define JSON_SPIFFS_KEY_STATS2NV	"saveStatsToNVS"
#define JSON_SPIFFS_KEY_INVCOLOR	"invertColors"
#define JSON_SPIFFS_KEY_BRIGHTNESS	"Brightness"
#define JSON_SPIFFS_KEY_CLOCKFORMAT	"clockFormat"
#define JSON_SPIFFS_KEY_SCREENBRIGHTNESS	"screenBrightness"

// settings
struct TSettings
{
	String WifiSSID{ DEFAULT_SSID };
	String WifiPW{ DEFAULT_WIFIPW };
	String PoolAddress{ DEFAULT_POOLURL };
	char BtcWallet[80]{ DEFAULT_WALLETID };
	char WorkerName[32]{ DEFAULT_WORKERNAME };
	char PoolPassword[80]{ DEFAULT_POOLPASS };
	int PoolPort{ DEFAULT_POOLPORT };
	int Timezone{ DEFAULT_TIMEZONE };            // legacy fixed offset (migration only)
	char PosixTZ[48]{ DEFAULT_POSIXTZ };         // POSIX TZ string (offset + DST rules)
	bool saveStats{ DEFAULT_SAVESTATS };
	bool invertColors{ DEFAULT_INVERTCOLORS };
	int Brightness{ DEFAULT_BRIGHTNESS };
	int clockFormat{ DEFAULT_CLOCKFORMAT };
	int screenBrightness{ DEFAULT_SCREENBRIGHTNESS };
	bool batterySaver{ true }; // Default on; a saved v4.3 choice takes precedence.
};

#endif // _STORAGE_H_
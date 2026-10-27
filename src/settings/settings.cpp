#include "settings.h"
#include <ctype.h>
#include <string.h>
namespace settings {
int brightnessPreset(int value) { return value <= 80 ? 60 : value <= 190 ? 150 : 230; }
const char* brightnessLabel(int value) { int p = brightnessPreset(value); return p == 60 ? "Low" : p == 150 ? "Medium" : "High"; }
int nextBrightness(int value) { int p = brightnessPreset(value); return p == 60 ? 150 : p == 150 ? 230 : 60; }
static void trim(char* text) {
  char* start = text;
  while (*start && isspace(static_cast<unsigned char>(*start))) ++start;
  if (start != text) memmove(text, start, strlen(start) + 1);
  size_t n = strlen(text);
  while (n && isspace(static_cast<unsigned char>(text[n-1]))) text[--n] = 0;
}
void normalize(TSettings& value) {
  value.BtcWallet[sizeof(value.BtcWallet)-1] = 0;
  value.WorkerName[sizeof(value.WorkerName)-1] = 0;
  value.PoolPassword[sizeof(value.PoolPassword)-1] = 0;
  value.PosixTZ[sizeof(value.PosixTZ)-1] = 0;
  trim(value.BtcWallet); trim(value.WorkerName);
  value.PoolAddress.trim();
  if (!value.PoolAddress.length()) value.PoolAddress = DEFAULT_POOLURL;
  if (value.PoolPort < 1 || value.PoolPort > 65535) value.PoolPort = DEFAULT_POOLPORT;
  if (value.clockFormat != 12 && value.clockFormat != 24) value.clockFormat = DEFAULT_CLOCKFORMAT;
  if (value.screenBrightness < 1 || value.screenBrightness > 255) value.screenBrightness = DEFAULT_SCREENBRIGHTNESS;
  value.screenBrightness = brightnessPreset(value.screenBrightness);
  if (value.Timezone < -12 || value.Timezone > 14) value.Timezone = DEFAULT_TIMEZONE;
}
}

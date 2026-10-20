#pragma once
#include "drivers/storage/storage.h"
namespace settings {
int brightnessPreset(int value);
const char* brightnessLabel(int value);
int nextBrightness(int value);
void normalize(TSettings& value);
}

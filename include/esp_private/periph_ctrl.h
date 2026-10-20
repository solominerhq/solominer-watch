// Shim header for GFX Library for Arduino compatibility
// with ESP32 Arduino Core 2.x (periph_ctrl moved to esp_private/ in Core 3.x).
#ifndef _ESP_PRIVATE_PERIPH_CTRL_H_
#define _ESP_PRIVATE_PERIPH_CTRL_H_
// Try to include the Core 2.x location, otherwise stub it out
#if __has_include("driver/periph_ctrl.h")
#include "driver/periph_ctrl.h"
#elif __has_include("periph_ctrl.h")
#include "periph_ctrl.h"
#endif
#endif

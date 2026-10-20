#ifndef QRCODE_H
#define QRCODE_H

#include <lvgl.h>

// The actual image exported from qrcode.c is named qrcode220x220
LV_IMG_DECLARE(qrcode220x220);

// Create an alias if code references 'qr_code'
#define qr_code qrcode220x220

#endif
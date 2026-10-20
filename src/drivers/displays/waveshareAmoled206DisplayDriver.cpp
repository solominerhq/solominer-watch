#include "ui/wifi_display_wake.h"
#include "ui/setup_shortcut.h"
#include "ui/battery_notice.h"
#include "device/clock_validation.h"
#include "settings/save_retry.h"
#include "mining.h"
#include <mutex>
#include "diagnostics/diagnostics.h"
#include "network/wifi_connection.h"
#include "ui/page_controls.h"
#include "ui/touch_input.h"
#include "device/block_record.h"
#include "ui/mining_status.h"
#include "ui/setup_code.h"
#include "device/device_power.h"
#include "device/sleep_control.h"
#include "device/battery_reading.h"
#include "device/mining_control.h"
#include "settings/settings.h"
#include "displayDriver.h"

#ifdef WAVESHARE_AMOLED_206_DISPLAY

#include "../../seriallog.h"
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "ui/clock_stars.h"
#include <Wire.h>
#include <WiFi.h>
#include <atomic>
#include <XPowersLib.h>
#include <time.h>
#include "freertos/semphr.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "version.h"
#include "monitor.h"
#include "inter_fonts.h"
#include "qrcode.h"
#if LV_USE_QRCODE
#include <src/libs/qrcode/qrcodegen.h>
#endif
#include "Astronaut_200x200.h"
#include "block_found_scene.h"
#include "../audio/audio.h"
#include "wManager.h"


// External monitor data
extern monitor_data mMonitor;
extern volatile uint32_t valids; // from mining.cpp — genuine blocks found
extern uint32_t Mhashes;         // from mining.cpp — total megahashes computed
extern uint32_t totalKHashes;    // from mining.cpp — total kilohashes (updates every second)
extern volatile uint32_t hashes; // from mining.cpp — sub-kilohash remainder (0-999999)
extern double best_diff;         // from mining.cpp — best difficulty ever found
extern void reboot_to_setup(void);  // from wManager.cpp — reopen the setup portal (wallet/WiFi preserved)
extern void suffix_string(double val, char *buf, size_t bufsiz, int sigdigits);

// Last-found-block info (from mining.cpp) for the celebration screen.


// External storage — needed for settings screen to persist changes
#include "drivers/storage/nvMemory.h"
extern nvMemory nvMem;

// Null-guard macro: returns from current function if LVGL object creation failed
#define LV_NULL_CHECK(obj) do { if ((obj) == NULL) { SERIAL_PRINTLN("[LVGL] NULL object - out of memory?"); return; } } while(0)
#define LV_NULL_CHECK_VAL(obj, retval) do { if ((obj) == NULL) { SERIAL_PRINTLN("[LVGL] NULL object - out of memory?"); return (retval); } } while(0)

// External settings for wallet/pool info
#include "drivers/storage/storage.h"
extern TSettings Settings;

// PCF85063 RTC I2C address and registers
#define PCF85063_ADDR    0x51
#define PCF85063_INT_PIN 39
#define PCF85063_SEC     0x04
#define PCF85063_MIN     0x05
#define PCF85063_HOUR    0x06
#define PCF85063_DAY     0x07
#define PCF85063_WDAY    0x08
#define PCF85063_MONTH   0x09
#define PCF85063_YEAR    0x0A

// QR Code image
LV_IMG_DECLARE(qrcode220x220);


// =============================================================================
// TOUCH DRIVER - FT3168 (Waveshare ESP32-S3-Touch-AMOLED-2.06)
// =============================================================================

#define TOUCH_RST_PIN   9
#define TOUCH_INT_PIN   38
#define TOUCH_SDA_PIN   15
#define TOUCH_SCL_PIN   14

// Wake-on-touch: FT3168 asserts INT pin LOW on touch events.
// ISR sets this flag so lv_tick_task can wake the display without I2C polling.
static volatile bool touchWakeFlag = false;
static TaskHandle_t touchTaskHandle = nullptr;

static void IRAM_ATTR touchWakeISR() {
    touchWakeFlag = true;
    BaseType_t woke=pdFALSE;
    if (touchTaskHandle) vTaskNotifyGiveFromISR(touchTaskHandle,&woke);
    if (woke) portYIELD_FROM_ISR();
}

#define FT3168_I2C_ADDR      0x38

#define FT_REG_TD_STATUS     0x02
#define FT_REG_P1_XH         0x03
#define FT_REG_CHIP_ID       0xA3
#define FT_REG_FW_VER        0xA6
#define FT_REG_THGROUP       0x80   // Touch sensitivity threshold (lower = more sensitive)
#define FT_REG_THPEAK        0x81   // Peak detection threshold
#define FT_REG_CTRL          0x86   // Power mode: 0=active, 1=monitor
#define FT_REG_TIMEENTERMONITOR 0x87 // Time before entering monitor mode

typedef struct {
    uint16_t x;
    uint16_t y;
    bool touched;
    uint8_t pressure;
    uint8_t gesture;
} touch_point_t;

static bool touchInitialized = false;
static uint8_t touchI2CAddr = 0;

// RTC helper functions
static bool rtcInitialized = false;

struct RTCDateTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint16_t year;
};

// =============================================================================
// I2C TRANSACTION MUTEX
// =============================================================================
// Serializes multi-call I2C transactions on the shared Wire bus. Three tasks
// talk to it: lv_tick_task (touch @50Hz), the Monitor task (PMU battery reads
// during draws) and RTC reads. Arduino's TwoWire lock is per-call, NOT
// per-transaction, so a "write register pointer, then requestFrom" sequence
// from one task can be interleaved by another task between the two calls,
// yielding corrupted battery%/RTC/touch values. Take this around every
// logical transaction. NULL until created in waveshareDisplay_Init — lock
// helpers treat NULL as "single-threaded init phase, no locking needed".
SemaphoreHandle_t i2c_mutex = NULL;

static inline bool i2cLock(uint32_t timeoutMs) {
    if (i2c_mutex == NULL) return true;  // pre-scheduler init: single-threaded
    return xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static inline void i2cUnlock() {
    if (i2c_mutex != NULL) xSemaphoreGive(i2c_mutex);
}

// Exported so the audio module can serialize its ES8311 register writes (which
// go over the same shared I2C bus, port 0) against touch/RTC/PMU access.
bool displayI2cLock(uint32_t timeoutMs) { return i2cLock(timeoutMs); }
void displayI2cUnlock() { i2cUnlock(); }

static uint8_t bcd2dec(uint8_t bcd) {
    return ((bcd / 16) * 10) + (bcd % 16);
}

static uint8_t dec2bcd(uint8_t dec) {
    return ((dec / 10) * 16) + (dec % 10);
}

static bool writeRTC(RTCDateTime* dt) {
    if (!i2cLock(100)) return false;
    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(PCF85063_SEC);
    Wire.write(dec2bcd(dt->second) & 0x7F);
    Wire.write(dec2bcd(dt->minute) & 0x7F);
    Wire.write(dec2bcd(dt->hour) & 0x3F);
    Wire.write(dec2bcd(dt->day) & 0x3F);
    Wire.write(dec2bcd(dt->weekday) & 0x07);
    Wire.write(dec2bcd(dt->month) & 0x1F);
    Wire.write(dec2bcd(dt->year - 2000));
    bool ok = (Wire.endTransmission() == 0);
    i2cUnlock();
    return ok;
}

static bool readRTC(RTCDateTime* dt) {
    if (!rtcInitialized) return false;
    if (!i2cLock(100)) return false;

    // Register-pointer write + read must be one atomic transaction —
    // another task interleaving between them would corrupt the read.
    Wire.beginTransmission(PCF85063_ADDR);
    Wire.write(PCF85063_SEC);
    if (Wire.endTransmission() != 0) { i2cUnlock(); return false; }

    if (Wire.requestFrom(PCF85063_ADDR, 7) != 7) { i2cUnlock(); return false; }

    uint8_t raw[7];
    for(auto& value:raw)value=Wire.read();
    i2cUnlock();
    if(!clock_validation::registers(raw))return false;
    dt->second=bcd2dec(raw[0]);dt->minute=bcd2dec(raw[1]);
    dt->hour=bcd2dec(raw[2]);dt->day=bcd2dec(raw[3]);dt->weekday=raw[4];
    dt->month=bcd2dec(raw[5]);dt->year=2000+bcd2dec(raw[6]);

    return true;
}


static bool initRTC() {
    Wire.beginTransmission(PCF85063_ADDR);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        SERIAL_PRINTLN("[RTC] PCF85063 found at 0x51");
        rtcInitialized = true;
        return true;
    }
    
    SERIAL_PRINTLN("[RTC] PCF85063 not found");
    rtcInitialized = false;
    return false;
}

bool touch_init(void) {
    SERIAL_PRINTLN("[TOUCH] Initializing FT3168 touch controller...");
    
    pinMode(TOUCH_INT_PIN, INPUT);
    
    pinMode(TOUCH_RST_PIN, OUTPUT);
    digitalWrite(TOUCH_RST_PIN, LOW);
    delay(10);
    digitalWrite(TOUCH_RST_PIN, HIGH);
    delay(300);
    
    Wire.beginTransmission(FT3168_I2C_ADDR);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        SERIAL_PRINTLN("[TOUCH] FT3168 found at 0x38");
        
        Wire.beginTransmission(FT3168_I2C_ADDR);
        Wire.write(FT_REG_CHIP_ID);
        Wire.endTransmission();
        Wire.requestFrom((int)FT3168_I2C_ADDR, 1);
        if (Wire.available()) {
            uint8_t chipId = Wire.read();
            SERIAL_PRINTF("[TOUCH] FT3168 Chip ID: 0x%02X\n", chipId);
        }
        
        touchI2CAddr = FT3168_I2C_ADDR;
        touchInitialized = true;
        SERIAL_PRINTLN("[TOUCH] FT3168 initialized successfully!");
        return true;
    }
    
    SERIAL_PRINTF("[TOUCH] FT3168 not found (error=%d)\n", error);
    touchInitialized = false;
    return false;
}

bool touch_read(touch_point_t *point) {
    if (point == NULL || touchI2CAddr == 0) return false;
    
    point->touched = false;
    point->gesture = 0;
    point->x = 0;
    point->y = 0;
    point->pressure = 0;
    
    if (!i2cLock(50)) return false;
    Wire.beginTransmission(touchI2CAddr);
    Wire.write(FT_REG_TD_STATUS);
    if (Wire.endTransmission() != 0) { i2cUnlock(); return false; }

    Wire.requestFrom((int)touchI2CAddr, 5);
    if (Wire.available() < 5) { i2cUnlock(); return false; }

    uint8_t touchCount = Wire.read() & 0x0F;
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();
    i2cUnlock();

    if (touchCount > 2) return false;
    
    uint8_t event = (xh >> 6) & 0x03;
    point->touched = (touchCount > 0) && (event != 1) && (event != 3);
    
    if (point->touched) {
        point->x = ((xh & 0x0F) << 8) | xl;
        point->y = ((yh & 0x0F) << 8) | yl;
        point->pressure = 100;
        
        // Clamp to the last valid pixel index. The panel is 410x502, so the
        // max index is 409/501 (one less than the count), not 410/502.
        if (point->x > 409) point->x = 409;
        if (point->y > 501) point->y = 501;
    }
    
    return true;
}

// =============================================================================
// GESTURE ENGINE
// =============================================================================

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,
    GESTURE_DOUBLE_TAP,
    GESTURE_LONG_PRESS,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_DRAG
} gesture_type_t;

typedef struct {
    gesture_type_t type;
    int16_t start_x, start_y;
    int16_t end_x, end_y;
    uint32_t duration_ms;
    float velocity;
} gesture_event_t;

static uint32_t lastWakeTime = 0;
static uint32_t accountedSleepMs = 0;
static ui::TouchGesture touchGesture;
static ui::TouchQueue touchQueue;
static std::atomic<uint32_t> inputRevision{1};

// =============================================================================
// LVGL MUTEX
// =============================================================================

SemaphoreHandle_t lvgl_mutex = NULL;
// Serializes ALL writes to the physical QSPI display bus. The LVGL frame flush
// (render task) and setBrightness()/sleep commands (which can run from other
// tasks like lv_tick_task) both drive the same bus; without this they can
// interleave mid-transaction and corrupt the peripheral → panic/reboot. This is
// a separate lock from lvgl_mutex on purpose: setBrightness() is sometimes called
// from code already holding lvgl_mutex, so reusing it would deadlock.
SemaphoreHandle_t bus_mutex = NULL;

// Serializes the compound display power-state transitions (the multi-step
// sleep sequence in updateSmartDimming on the Monitor task vs. the wake
// sequence in wakeDisplay on lv_tick_task). Without it, a wake landing between
// the sleep's flag-set and brightness-write can strand the panel dark while
// the flags say "on" — after which a PWR press powers the device off instead
// of waking it. Ordered strictly AFTER lvgl_mutex and BEFORE bus_mutex (both
// transition holders may already hold lvgl; both call bus writes underneath),
// so no inversion. Scoped to the flag+brightness commit only, never held
// across the WiFi restart.
SemaphoreHandle_t disp_state_mutex = NULL;

// =============================================================================
// DISPLAY HARDWARE
// =============================================================================

#ifndef LCD_CS
#define LCD_SDIO0   4
#define LCD_SDIO1   5
#define LCD_SDIO2   6
#define LCD_SDIO3   7
#define LCD_SCLK    11
#define LCD_CS      12
#define LCD_RESET   8
#endif

#define PMU_SDA     15
#define PMU_SCL     14

#define WIDTH       410
#define HEIGHT      502
// LV_BUF_LINES removed — full-screen PSRAM framebuffer used instead (see waveshareDisplay_Init)
#define SAFE_TOP     30
#define SAFE_BOTTOM  30
#define SAFE_LEFT    20
#define SAFE_RIGHT   20

// On-device adjustable settings (Brightness) live on the Settings
// screen, where taps cycle/toggle them using shared page hit targets.
// WiFi / wallet / pool / timezone are configured in the setup portal.
// The dim timeout is fixed at DIM_TIMEOUT_S — deliberately not user-selectable.

// =============================================================================
// THEME - AMOLED Optimized
// =============================================================================

#define THEME_BG_PRIMARY    lv_color_black()
#define THEME_BG_CARD       lv_color_hex(0x1C1C1E)
#define THEME_TEXT_WHITE    lv_color_white()
#define THEME_TEXT_MUTED    lv_color_hex(0xD0D0D6)
#define BATTERY_OUTLINE_COLOR lv_color_hex(0x64748B) // Independent of supporting text brightness.
#define THEME_TEXT_DARK     lv_color_hex(0x374151)
#define THEME_ORANGE      lv_color_hex(0xF97316)
#define THEME_GREEN         lv_color_hex(0x34C759)
#define THEME_YELLOW        lv_color_hex(0xFBBF24)
#define THEME_RED           lv_color_hex(0xFF3B30)   // vivid true red (0xF87171 read as pink on the AMOLED)

// =============================================================================
// STATE VARIABLES
// =============================================================================

// Accessed under lvgl_mutex after display initialization.
static bool splashActive = false;
static unsigned long splashStartMs = 0;
static constexpr uint32_t SPLASH_STARS_MS = 250;
static constexpr uint32_t SPLASH_ASTRONAUT_MS = 1250;
static constexpr uint32_t SPLASH_MIN_MS = SPLASH_STARS_MS + SPLASH_ASTRONAUT_MS;

static int currentScreenIndex = 0;  // Default to Clock (NAV_CLOCK=0) - clock is left, mining is right
static int lastBuiltScreen = -1;   // Track which screen's LVGL objects are currently built (-1 = none)
static uint8_t currentBrightness = 150;  // Match DISPLAY_WAKE_BRIGHTNESS
static bool showingMiningStats = false;
static int forcedScreenIndex = -1;
static bool setupShowingDetails = false;  // false = QR code, true = manual details
static bool inSetupMode = false;  // Track if we're showing setup screen
static volatile bool setupRedrawRequested = false;  // Set by toggle, drawn by dispatcher

// Charging boot mode — when device powers on via USB (not PWR button press),
// show a charging screen instead of fully booting. BOOT press starts the watch.
volatile bool chargingBootMode = false;
static ui::SetupShortcut setupShortcut;
static std::atomic<bool> setupButtonsConsumed{false};

#define BOOT_BUTTON_PIN 0
#define PWR_BUTTON_PIN 10
static uint32_t lastButtonPress = 0;
#define BUTTON_DEBOUNCE_MS 150
static volatile bool displayManuallyOff = false;
static volatile bool displayAwake = true;
static volatile bool displayOff = false;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void forceScreenSwitch(int screenIndex);
void wakeDisplay(void);
void updateSmartDimming(void);
void setBrightness(uint8_t level);
uint8_t getBrightness(void);
void toggleSetupScreen(void);
void checkWaveshareButtons(void);
void waveshareButtonClick(void);
// Settings screen helpers (defined later, needed by gesture handler)
static volatile int settingsTappedAction = static_cast<int>(ui::PageAction::None);
static ui::SettingsView settingsView = ui::SettingsView::List;
static int setupReturnScreen = 3;
static ui::SetupView setupView = ui::SetupView::Qr;
static bool setupConnectFailed = false;
static void handleSetupAction(ui::SetupAction action);
static String sanitizeForFont(const String& raw);

// =============================================================================
// IMMEDIATE REDRAW FLAG - volatile for cross-task access
// =============================================================================

std::atomic<bool> needsImmediateRedraw{false};
static std::atomic<bool> panelRedrawRequested{false};

void triggerImmediateRedraw(void) {
    panelRedrawRequested.store(true);
    needsImmediateRedraw.store(true);
}

// Force full LVGL object rebuild on next screen draw.
// Called when switching screens, exiting setup mode, etc.
void invalidateScreen(void) {
    inputRevision.fetch_add(1);
    settingsView = ui::SettingsView::List;
    setupReturnScreen = 3;
    settingsTappedAction = static_cast<int>(ui::PageAction::None);
    lastBuiltScreen = -1;
}

// =============================================================================
// AUTO-SWITCH LOGIC
// =============================================================================

void forceScreenSwitch(int screenIndex) {
    forcedScreenIndex = screenIndex;
}

int getAndClearForcedScreen() {
    int result = forcedScreenIndex;
    forcedScreenIndex = -1;
    return result;
}


// =============================================================================
// NAVIGATION STRUCTURE - four main pages
// =============================================================================

#define NUM_MAIN_SCREENS 4

#define NAV_CLOCK     0
#define NAV_MINING    1
#define NAV_DEVICE    2
#define NAV_SETTINGS  3   // Settings screen — swipe order: Clock > Mining > Device > Settings

struct TouchTarget { lv_obj_t* object; ui::PageRect bounds; };
static TouchTarget touchTargets[12]{};
static portMUX_TYPE targetMux = portMUX_INITIALIZER_UNLOCKED;
static lv_obj_t* pressedTouchObject = nullptr;
static void unregisterTouchTarget(lv_event_t* event) {
    auto* object = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (pressedTouchObject == object) pressedTouchObject = nullptr;
    portENTER_CRITICAL(&targetMux);
    for (auto& target : touchTargets) if (target.object == object) target.object = nullptr;
    portEXIT_CRITICAL(&targetMux);
}
static void registerTouchTarget(lv_obj_t* object, const ui::PageRect& bounds, bool primary=false) {
    lv_obj_set_style_bg_color(object, lv_color_hex(primary ? 0xFF8A34 : 0x242529), LV_STATE_PRESSED);
    lv_obj_add_event_cb(object, unregisterTouchTarget, LV_EVENT_DELETE, nullptr);
    portENTER_CRITICAL(&targetMux);
    for (auto& target : touchTargets) if (!target.object) {
        target.object=object; target.bounds=bounds; break;
    }
    portEXIT_CRITICAL(&targetMux);
}
static bool touchBounds(int x, int y, ui::PageRect& bounds) {
    bool found=false;
    portENTER_CRITICAL(&targetMux);
    for (const auto& target : touchTargets) if (target.object && target.bounds.contains(x,y)) {
        bounds=target.bounds; found=true; break;
    }
    portEXIT_CRITICAL(&targetMux);
    return found;
}
static lv_obj_t* pageText(const char* text, const lv_font_t* font,
                          lv_color_t color, int y) {
    auto* label = lv_label_create(lv_scr_act());
    if (!label) return nullptr;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_width(label, 346);
    lv_obj_set_pos(label, 32, y);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}
static lv_obj_t* pageAction(const ui::PageRect& bounds, const char* text, bool primary = false) {
    auto* button = lv_obj_create(lv_scr_act());
    if (!button) return nullptr;
    lv_obj_set_pos(button, bounds.x, bounds.y);
    lv_obj_set_size(button, bounds.width, bounds.height);
    lv_obj_set_style_bg_color(button, primary ? THEME_ORANGE : lv_color_hex(0x151619), 0);
    lv_obj_set_style_border_width(button, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x303238), 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    auto* label = lv_label_create(button);
    if (!label) { lv_obj_delete(button); return nullptr; }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &inter_28, 0);
    lv_obj_set_style_text_color(label, primary ? lv_color_black() : THEME_TEXT_WHITE, 0);
    lv_obj_center(label);
    registerTouchTarget(button, bounds, primary);
    return button;
}

static bool buildDetailHeader(const char* title) {
    auto* back = pageAction(ui::detailBack, "");
    if (back) {
        lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(back, 0, 0);
        auto* icon = lv_obj_get_child(back, 0);
        lv_label_set_text(icon, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    }
    auto* label = pageText(title, &inter_28_semi_bold, THEME_TEXT_WHITE, 35);
    if (label) {
        lv_obj_set_pos(label, 82, 35);
        lv_obj_set_width(label, 296);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    }
    return back && label;
}

static lv_color_t statusColor(ui::StatusTone tone) {
    return (tone == ui::StatusTone::Wifi || tone == ui::StatusTone::Attention) ? lv_color_hex(0xF97316)
         : tone == ui::StatusTone::Neutral ? lv_color_hex(0x8E8E93)
         : tone == ui::StatusTone::Good ? THEME_GREEN : lv_color_hex(0x3B82F6);
}
static void clearPage() {
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), THEME_BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}
static void pageMessage(const char* title, const char* detail, bool warning = false) {
    clearPage();
    pageText(title, &inter_36_bold, warning ? lv_color_hex(0xF97316) : THEME_TEXT_WHITE, 128);
    pageText(detail, &inter_28, THEME_TEXT_MUTED, 220);
}
#if LV_USE_QRCODE
// LVGL 9 centers whole-pixel QR modules inside the canvas. Add only the
// remaining four-module quiet zone, accounting for that built-in white space.
// Keep this calculation paired with lv_qrcode_update's version/scale choice.
static bool updateWatchQr(lv_obj_t* qr, const char* payload, int canvasSize) {
    const size_t bytes = strlen(payload);
    int version = qrcodegen_getMinFitVersion(qrcodegen_Ecc_MEDIUM, bytes);
    if (version <= 0) return false;
    const int scale = canvasSize / qrcodegen_version2size(version);
    if (scale <= 0) return false;
    for (int next=version+1; next<qrcodegen_VERSION_MAX; ++next) {
        if (qrcodegen_version2size(next)*scale > canvasSize) break;
        version = next;
    }
    const int innerMargin = (canvasSize-qrcodegen_version2size(version)*scale)/2;
    const int border = 4*scale > innerMargin ? 4*scale-innerMargin : 0;
    lv_qrcode_set_size(qr, canvasSize);
    auto* buffer = lv_canvas_get_draw_buf(qr);
    if (!buffer || buffer->header.w != canvasSize) return false;
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    if (lv_qrcode_update(qr, payload, bytes) != LV_RESULT_OK) return false;
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, border, 0);
    lv_obj_update_layout(qr);
    return true;
}
#endif
static bool pageQr(const char* payload, int centerY) {
#if LV_USE_QRCODE
    auto* qr = lv_qrcode_create(lv_scr_act());
    if (!qr) return false;
    if (!updateWatchQr(qr, payload, 188)) { lv_obj_delete(qr); return false; }
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, centerY-lv_obj_get_height(qr)/2);
    return true;
#else
    (void)payload; (void)centerY; return false;
#endif
}
static void fitSingleLine(lv_obj_t* value, int width) {
    lv_obj_set_width(value, width);
    lv_obj_set_height(value, inter_28.line_height);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
}

// =============================================================================
// BLOCK FOUND CELEBRATION
// =============================================================================

static int lastBlocksFound = 0;
static bool showingBlockFoundScreen = false;
static unsigned long blockFoundAnimStart = 0;
static bool houstonBuilt = false;          // "The Moment" screen build latch (see below)

// Song-synchronized landing and flag strike lead to a reward record.
// Swipes/BOOT change pages; the footer closes. Manual navigation stops the tour.
static ui::block_found::Tour celebrationTour;
static uint8_t celebrationPage = 0; // 0 victory, 1 reward, 2 block / explorer
static bool certBuilt = false;             // certificate build latch
// Landing-sequence pre-roll (plays before the Houston screen)
static bool showingLandingSeq = false;
static bool landingBuilt = false;
static bool celebrationHasPlayback = false;
// The audio API retains the last track's position after stopping. Only read it
// once this celebration has requested its own playback and reset that clock.
static bool celebrationClockArmed = false;
static block_record::Record celebrationRecord;
static std::atomic<bool> navigationClickPending{false};
static bool celebrationProtectsPlayback();
static void clearDemoControls();
static lv_obj_t *g_landingImg = NULL;

void dismissBlockFoundScreen() {
    celebrationClockArmed=false;
    clearDemoControls();
    clearPage(); // Delete scene/heading timers now, before a rapid replay.
    invalidateScreen();
    celebrationHasPlayback=false;
    celebrationTour={};
    showingBlockFoundScreen = false;
    showingLandingSeq = false;
    landingBuilt = false;
    g_landingImg = nullptr;
    houstonBuilt = false;   // rebuild the celebration scene fresh next time
    celebrationPage = 0;
    certBuilt = false;
    audioStopBlockFound();  // cut the music if the celebration is dismissed
}

// =============================================================================
// BLOCK FOUND SIMULATION (secret easter egg — hold BOOT button 5s from a main screen)
//
// Never touches the real `valids` counter (persisted to NVS flash).
// Sample receipt data lives only in the demo renderer; real block fields stay intact.
// A three-second countdown leads to the same celebration with no real-record side effects.
// =============================================================================

static bool simCountdownActive = false;
static bool simCelebrationActive = false;
static unsigned long simCountdownStart = 0;
static int simLastCountdown = -1;
static lv_obj_t *simCountdownLabel = NULL;
#define SIM_COUNTDOWN_SECS    3

// BOOT long-press triggers simulation (set by pollBootButton on lv_tick_task)
static volatile bool bootLongPressFlag = false;
static bool bootWakeConsumed = false; // Sleep-wake press must not also change page on release.

static int simReturnScreen = NAV_MINING;
static lv_obj_t* demoControls = nullptr;
static lv_obj_t* demoMuteLabel = nullptr;
static lv_obj_t* demoDots[3] = {};
static lv_obj_t* demoDotsBackground = nullptr;
static void updateDemoControls();

static void clearDemoControls() {
    if (demoControls) lv_obj_delete(demoControls);
    demoControls = nullptr;
    demoMuteLabel = nullptr;
    for (auto& dot:demoDots) dot=nullptr;
    demoDotsBackground=nullptr;
}

static void buildDemoControls() {
    if (simCountdownActive || (!showingLandingSeq && !showingBlockFoundScreen) || demoControls) return;
    // Separate layer survives page changes; dismissal deletes it and its touch targets.
    demoControls = lv_obj_create(lv_layer_top());
    if (!demoControls) return;
    lv_obj_remove_style_all(demoControls);
    lv_obj_set_size(demoControls, WIDTH, HEIGHT);
    lv_obj_set_pos(demoControls, 0, 0);
    lv_obj_clear_flag(demoControls, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    auto* pill=lv_obj_create(demoControls);
    demoDotsBackground=pill;
    const int dotsY = HEIGHT - SAFE_BOTTOM + 4; // Same baseline as createPageDots.
    lv_obj_remove_style_all(pill);lv_obj_set_pos(pill,148,dotsY-5);lv_obj_set_size(pill,114,28);
    lv_obj_set_style_radius(pill,14,0);lv_obj_set_style_bg_color(pill,lv_color_hex(0x080A0C),0);
    lv_obj_set_style_bg_opa(pill,238,0);
    lv_obj_clear_flag(pill,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    for(int i=0;i<3;++i){
        auto* dot=lv_obj_create(demoControls);demoDots[i]=dot;lv_obj_remove_style_all(dot);
        lv_obj_set_pos(dot,163+i*33,dotsY);lv_obj_set_size(dot,18,18);lv_obj_set_style_radius(dot,9,0);
        lv_obj_set_style_bg_opa(dot,255,0);
        lv_obj_clear_flag(dot,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    }
    for (int i = 0; i < 2; ++i) {
        if (i==0 && (!celebrationHasPlayback || !audioHasBlockFoundTrack())) continue;
        const auto& bounds = i == 0 ? ui::demoMute : ui::demoExit;
        auto* button = lv_obj_create(demoControls);
        if (!button) continue;
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, bounds.x, bounds.y);
        lv_obj_set_size(button, bounds.width, bounds.height);
        lv_obj_set_style_radius(button, bounds.width / 2, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x080A0C), 0);
        lv_obj_set_style_bg_opa(button, 238, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x45474D), 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_clear_flag(button, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        auto* label = lv_label_create(button);
        if (!label) continue;
        lv_label_set_text(label, i == 0 ? (audioBlockFoundMuted() ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX) : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(label, THEME_TEXT_WHITE, 0);
        lv_obj_center(label);
        registerTouchTarget(button,bounds);
        if (i == 0) demoMuteLabel = label;
    }

    updateDemoControls();
}

static void beginSimulationPlayback() {
    celebrationClockArmed=false;
    simCountdownActive = false;
    simCelebrationActive = true;
    celebrationHasPlayback = true;
    celebrationRecord={};
    celebrationTour={};
    simCountdownLabel = nullptr;
    showingLandingSeq = true;
    showingBlockFoundScreen = false;
    landingBuilt = false;
    houstonBuilt = false;
    celebrationPage = 0;
    certBuilt = false;
    blockFoundAnimStart = millis();
    wakeDisplay();
    triggerImmediateRedraw();
}

static void startBlockFoundSimulation(bool countdown = true) {
    diagnostics::event(diagnostics::Event::DemoStart);
    simReturnScreen = currentScreenIndex;
    dismissBlockFoundScreen();
    // A deliberate new demo always starts with sound; mute applies to this run.
    audioSetBlockFoundMuted(false);
    audioPrewarm();
    simCountdownActive = countdown;
    simCelebrationActive = false;
    celebrationHasPlayback = true;
    celebrationTour={};
    simCountdownStart = millis();
    simLastCountdown = -1;
    simCountdownLabel = nullptr;
    wakeDisplay();
    if (!countdown) beginSimulationPlayback();
}

static uint32_t countdownElapsedMs(){return uint32_t(millis()-simCountdownStart);}
static void renderSimCountdown() {
    const int remaining=SIM_COUNTDOWN_SECS-int(countdownElapsedMs()/1000);
    if(remaining<=0){beginSimulationPlayback();return;}
    if(!simCountdownLabel){
        clearPage();invalidateScreen();
        pageText("BLOCK FOUND DEMO", &inter_28, THEME_TEXT_WHITE, 108);
        simCountdownLabel=pageText("", &inter_120, THEME_TEXT_WHITE, 177);
        auto* cancel = pageAction(ui::bottomAction, "Cancel");
        if (cancel) lv_obj_set_style_radius(cancel, 18, 0);
    }
    if(simCountdownLabel && remaining!=simLastCountdown){
        simLastCountdown=remaining;
        lv_label_set_text_fmt(simCountdownLabel,"%d",remaining);
    }
}

static void endSimulation() {
    diagnostics::event(simCountdownActive ? diagnostics::Event::DemoCancel : diagnostics::Event::DemoEnd);
    simCelebrationActive = false;
    simCountdownActive = false;
    simCountdownLabel = nullptr;
    clearDemoControls();
    currentScreenIndex = simReturnScreen;
}

static void exitSimulation() {
    endSimulation();
    dismissBlockFoundScreen();
    wakeDisplay();
    invalidateScreen();
    triggerImmediateRedraw();
}

// Shared dismissal: a deliberate close acknowledges only the displayed record.
static void closeCelebration() {
    if (simCelebrationActive || simCountdownActive) { exitSimulation(); return; }
    if (celebrationRecord.valid()) block_record::review(celebrationRecord.count);
    dismissBlockFoundScreen();
    currentScreenIndex=NAV_MINING;
    invalidateScreen();wakeDisplay();triggerImmediateRedraw();
}
static void openSavedBlock() {
    const auto saved=block_record::snapshot();
    if (!saved.valid()) return;
    dismissBlockFoundScreen();
    celebrationRecord=saved;
    block_record::review(saved.count);
    simCelebrationActive=simCountdownActive=false;
    showingBlockFoundScreen=true;celebrationPage=0;
    currentScreenIndex=NAV_MINING;
    invalidateScreen();wakeDisplay();triggerImmediateRedraw();
}

// Called only with lvgl_mutex held. A confirmed Saver shutdown chooses the
// clock once, without waking the panel or extending the inactivity timer.
static void applyBatterySaverScreenReset() {
    static bool resetPending=false;
    resetPending=network::takeSaverPauseEvent() || resetPending;
    if (!resetPending || celebrationProtectsPlayback()) return;
    resetPending=false;
    if (inSetupMode || chargingBootMode) return;
    const bool wasDemo = simCountdownActive || simCelebrationActive;
    if (wasDemo) endSimulation();
    if (wasDemo || showingLandingSeq || showingBlockFoundScreen) dismissBlockFoundScreen();
    currentScreenIndex = NAV_CLOCK;
    forcedScreenIndex = -1;
    invalidateScreen(); // Also close Settings details and discard queued taps.
    triggerImmediateRedraw();
}

// =============================================================================
// "HOUSTON, WE FOUND A BLOCK" — full-screen celebration (astronaut moon landing)
//
// Uses the original artwork with a steady headline and an explicit receipt.

// Insert thousands separators: 840000 -> "840,000".
static void formatWithCommas(uint32_t v, char* out, size_t n) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v);
    int len = strlen(tmp), commas = (len - 1) / 3, o = 0;
    for (int i = 0; i < len && o < (int)n - 1; i++) {
        out[o++] = tmp[i];
        int rem = len - 1 - i;
        if (rem > 0 && rem % 3 == 0 && commas > 0 && o < (int)n - 1) out[o++] = ',';
    }
    out[o] = '\0';
}

// Satoshis -> trimmed BTC string, e.g. 312500000 -> "3.125", 5000000000 -> "50".
static void formatBtc(uint64_t sats, char* out, size_t n) {
    snprintf(out, n, "%llu.%08llu",
             (unsigned long long)(sats / 100000000ULL),
             (unsigned long long)(sats % 100000000ULL));
    int len = strlen(out);
    while (len > 0 && out[len - 1] == '0') out[--len] = '\0';   // trim zeros
    if (len > 0 && out[len - 1] == '.') out[--len] = '\0';       // trim dot
}

// Shared by actual finds and the isolated demo fixture.
static uint32_t celebrationStartTick = 0;
static bool celebrationFallbackClock = false;
static uint32_t celebrationFallbackStart = 0;
static uint32_t celebrationElapsedMs() {
    // A countdown, a queued stop, or an unbuilt opening has no playback clock.
    // In particular, don't advance the tour using the previous song's position.
    if (!celebrationClockArmed) return 0;
    if (!audioHasBlockFoundTrack()) {
        // A silent build starts immediately; there is no codec startup to await.
        const uint32_t elapsed = lv_tick_elaps(celebrationStartTick);
        return elapsed < ui::block_found::detailsMs ? elapsed : ui::block_found::detailsMs;
    }
    uint32_t position = 0;
    if (!celebrationFallbackClock && audioBlockFoundPositionMs(position)) {
        if(audioIsPlaying() || position>=ui::block_found::detailsMs) return position;
        // A short/failed track must not leave the opening permanently awake.
        const uint32_t elapsed=lv_tick_elaps(celebrationStartTick)>position ? lv_tick_elaps(celebrationStartTick) : position;
        return elapsed<ui::block_found::detailsMs ? elapsed : ui::block_found::detailsMs;
    }
    const uint32_t since = lv_tick_elaps(celebrationStartTick);
    // Give hardware/decoder startup time without running ahead of the song.
    if (since < 500 || (audioIsPlaying() && since < 2500)) return 0;
    if(!celebrationFallbackClock){
        celebrationFallbackClock=true;celebrationFallbackStart=lv_tick_get();
    }
    const uint32_t elapsed=lv_tick_elaps(celebrationFallbackStart);
    return elapsed<ui::block_found::detailsMs ? elapsed : ui::block_found::detailsMs; // Silent playback ends on a still frame.
}

static uint32_t celebrationHeadingColor() {
    const uint32_t elapsed=celebrationHasPlayback?celebrationElapsedMs():0;
    return ui::block_found::headingColor(elapsed,
        celebrationHasPlayback && audioIsPlaying() && !celebrationFallbackClock);
}

static void updateDemoControls() {
    const bool visible=!simCountdownActive && (!showingLandingSeq ||
        ui::block_found::navigationReady(celebrationElapsedMs()));
    if(demoDotsBackground){
        if(visible)lv_obj_clear_flag(demoDotsBackground,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(demoDotsBackground,LV_OBJ_FLAG_HIDDEN);
    }
    for(int i=0;i<3;++i)if(demoDots[i]){
        if(visible)lv_obj_clear_flag(demoDots[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(demoDots[i],LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(demoDots[i],lv_color_hex(i==celebrationPage?0xFFAE32:0x647789),0);
    }
}
static void updateCelebrationTour() {
    if(!celebrationHasPlayback || !(showingLandingSeq||showingBlockFoundScreen))return;
    const int page=celebrationTour.update(millis(),celebrationElapsedMs());
    if(page>=0){
        showingLandingSeq=false;landingBuilt=false;showingBlockFoundScreen=true;
        celebrationPage=uint8_t(page);certBuilt=houstonBuilt=false;
        triggerImmediateRedraw();
    }
}

static bool celebrationProtectsPlayback() {
    return celebrationHasPlayback && (showingLandingSeq || showingBlockFoundScreen) &&
        (showingLandingSeq || audioIsPlaying());
}

static void landingDoneCb() {
    showingLandingSeq = false;
    landingBuilt = false;
    houstonBuilt = false;
    showingBlockFoundScreen = true;
    celebrationPage = 0;
    certBuilt = false;
    blockFoundAnimStart = millis();
}

static void renderLandingSequence() {
    if (!lv_scr_act() || landingBuilt || audioIsPlaying()) return; // Let an exited track finish stopping before replay.
    landingBuilt = true;
    clearPage(); invalidateScreen();
    celebrationFallbackClock = false;
    celebrationStartTick = lv_tick_get();
    g_landingImg = block_found_scene::create(lv_scr_act(),celebrationElapsedMs,true,landingDoneCb,false,celebrationHeadingColor);
    if (!g_landingImg) landingDoneCb();
    else lv_refr_now(NULL);
    celebrationStartTick = lv_tick_get();
    audioPlayBlockFound();
    celebrationClockArmed = true;
    buildDemoControls();
}

static void renderBlockFoundHouston() {
    if (!lv_scr_act() || houstonBuilt) return;
    clearPage(); invalidateScreen();
    houstonBuilt = block_found_scene::create(lv_scr_act(),celebrationHasPlayback ? celebrationElapsedMs : []()->uint32_t{return ui::block_found::detailsMs;},false,nullptr,false,celebrationHeadingColor) != nullptr;
}

static lv_obj_t* rewardText(lv_obj_t* parent, const char* value, const lv_font_t* font,
                            uint32_t color, int baseline) {
    lv_obj_t* label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_label_set_text(label,value);
    lv_obj_set_style_text_font(label,font,0);
    lv_obj_set_style_text_color(label,lv_color_hex(color),0);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_width(label,360);
    lv_obj_set_pos(label,25,baseline-font->line_height+font->base_line);
    return label;
}

// The card owns its timer, so swiping or closing cannot leave callbacks behind.
// Updating only the headings keeps the numbers and QR pixels stationary.
struct RewardHeadingAnimation {
    lv_obj_t* first;
    lv_obj_t* second;
    lv_timer_t* timer;
    uint32_t color;
};
static void animateRewardHeadings(lv_obj_t* card,lv_obj_t* first,lv_obj_t* second=nullptr) {
    if (!celebrationHasPlayback) return;
    auto* state=new(std::nothrow)RewardHeadingAnimation{first,second,nullptr,celebrationHeadingColor()};
    if (!state) return;
    state->timer=lv_timer_create([](lv_timer_t* timer){
        auto* s=static_cast<RewardHeadingAnimation*>(lv_timer_get_user_data(timer));
        const uint32_t color=celebrationHeadingColor();
        if(!audioIsPlaying())lv_timer_pause(timer);
        if(color==s->color)return;
        s->color=color;
        for(auto* title:{s->first,s->second})
            if(title)lv_obj_set_style_text_color(title,lv_color_hex(color),0);
    },50,state);
    if (!state->timer) { delete state;return; }
    lv_obj_add_event_cb(card,[](lv_event_t* event){
        auto* s=static_cast<RewardHeadingAnimation*>(lv_event_get_user_data(event));
        lv_timer_delete(s->timer);delete s;
    },LV_EVENT_DELETE,state);
}

static bool rewardQr(lv_obj_t* card,const char* hash) {
#if LV_USE_QRCODE
    if (!ui::block_found::validHash(hash)) return false;
    lv_obj_t* qr=lv_qrcode_create(card);
    if (!qr) return false;
    char url[96]; snprintf(url,sizeof(url),"https://mempool.space/block/%s",hash);
    if (!updateWatchQr(qr,url,208)) { lv_obj_delete(qr); return false; }
    lv_obj_align(qr,LV_ALIGN_TOP_MID,0,244-lv_obj_get_height(qr)/2);
    return true;
#else
    (void)card; (void)hash; return false;
#endif
}
static void renderRewardCertificate(bool haveData,uint32_t height,uint64_t reward,const char* hash) {
    if (!lv_scr_act() || certBuilt) return;
    clearPage(); invalidateScreen();
    lv_obj_t* card=lv_obj_create(lv_scr_act());
    if (!card) return;
    certBuilt=true;
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card,0,0); lv_obj_set_size(card,WIDTH,HEIGHT);
    lv_obj_set_style_bg_color(card,lv_color_black(),0); lv_obj_set_style_bg_opa(card,255,0);
    lv_obj_clear_flag(card,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    clock_stars::create(card);
    if (celebrationPage==1) {
        auto* blockTitle=rewardText(card,"YOUR BLOCK",&inter_42_bold,celebrationHeadingColor(),108);
        if(blockTitle)lv_obj_set_style_text_letter_space(blockTitle,-1,0);
        char number[32];
        if(haveData && height) formatWithCommas(height,number,sizeof(number));
        else snprintf(number,sizeof(number),"Unavailable");
        char btc[32] = {};
        if (haveData && reward) formatBtc(reward,btc,sizeof(btc));
        const lv_font_t* unitFont=&inter_28;
        const int gap=12;
        // Use the same size for both numbers, fitting the wider complete row.
        const lv_font_t* valueFont=&inter_28;
        for (const lv_font_t* candidate : {&inter_64,&inter_56,&inter_48,&inter_36,&inter_28}) {
            lv_point_t numberSize,btcSize,unitSize;
            lv_text_get_size(&numberSize,number,candidate,0,0,1000,LV_TEXT_FLAG_NONE);
            lv_text_get_size(&btcSize,btc,candidate,0,0,1000,LV_TEXT_FLAG_NONE);
            lv_text_get_size(&unitSize,"BTC",unitFont,0,0,1000,LV_TEXT_FLAG_NONE);
            if ((!height || numberSize.x<=360) && (!reward || btcSize.x+gap+unitSize.x<=360)) {
                valueFont=candidate;break;
            }
        }
        rewardText(card,number,haveData && height ? valueFont : &inter_28,0xFFFFFF,190);
        auto* rewardTitle=rewardText(card,"BLOCK REWARD",&inter_42_bold,celebrationHeadingColor(),272);
        if(rewardTitle)lv_obj_set_style_text_letter_space(rewardTitle,-1,0);
        if (haveData && reward) {
            const lv_font_t* font=valueFont;
            lv_point_t amountSize,unitSize;
            lv_text_get_size(&amountSize,btc,font,0,0,1000,LV_TEXT_FLAG_NONE);
            lv_text_get_size(&unitSize,"BTC",unitFont,0,0,1000,LV_TEXT_FLAG_NONE);
            const int x=(WIDTH-amountSize.x-gap-unitSize.x)/2;
            const int y=352-font->line_height+font->base_line;
            auto* amountLabel=rewardText(card,btc,font,0xFFFFFF,352);
            auto* unitLabel=rewardText(card,"BTC",unitFont,0xFFFFFF,352);
            if(amountLabel){lv_obj_set_width(amountLabel,amountSize.x);lv_obj_set_pos(amountLabel,x,y);}
            if(unitLabel){
                lv_obj_set_width(unitLabel,unitSize.x);
                lv_obj_set_pos(unitLabel,x+amountSize.x+gap,
                    y+font->line_height-font->base_line-unitFont->line_height+unitFont->base_line);
            }
        } else rewardText(card,"Unavailable",&inter_36,0xFFFFFF,352);
        animateRewardHeadings(card,blockTitle,rewardTitle);
    } else {
        auto* title=rewardText(card,"SEE YOUR BLOCK",&inter_42_bold,celebrationHeadingColor(),90);
        if(title)lv_obj_set_style_text_letter_space(title,-1,0);
        if(!(haveData && rewardQr(card,hash)))
            rewardText(card,"Check your pool\nfor block details",&inter_28,0xC6CED8,270);
        animateRewardHeadings(card,title);
    }
    updateDemoControls();

}
static void renderDemoCertificate() {
    renderRewardCertificate(true,840000,312500000ULL,
        "0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5");
}
static void renderCertificate() {
    if (simCelebrationActive) { renderDemoCertificate(); return; }
    renderRewardCertificate(celebrationRecord.valid(),celebrationRecord.height,
        celebrationRecord.subsidy,celebrationRecord.hash);
}

static void advanceCelebrationPage(bool forward) {
    if (!(showingLandingSeq || showingBlockFoundScreen)) return;
    if (showingLandingSeq && !ui::block_found::navigationReady(celebrationElapsedMs())) return;
    celebrationTour.manual=true;
    showingLandingSeq=false;landingBuilt=false;showingBlockFoundScreen=true;
    celebrationPage=ui::block_found::nextPage(celebrationPage,forward);
    certBuilt=false;houstonBuilt=false;
    if(!simCelebrationActive && celebrationRecord.valid()) block_record::review(celebrationRecord.count);
    triggerImmediateRedraw();
}


// =============================================================================
// GESTURE HANDLER
// =============================================================================

// Called only under lvgl_mutex. Changes invalidate contacts and queued actions.
static void syncInputContext() {
    static uint32_t previous=~uint32_t(0);
    const uint32_t key=uint32_t(currentScreenIndex) | (uint32_t(settingsView)<<4) |
        (uint32_t(setupView)<<6) | (uint32_t(inSetupMode)<<9) |
        (uint32_t(chargingBootMode)<<10) | (uint32_t(simCountdownActive)<<11) |
        (uint32_t(showingLandingSeq)<<12) | (uint32_t(showingBlockFoundScreen)<<13) |
        (uint32_t(simCelebrationActive)<<14) | (uint32_t(splashActive)<<15) |
        (uint32_t(celebrationPage)<<16);
    if (key != previous) { previous=key; inputRevision.fetch_add(1); }
}
struct InputContextAfterDraw { ~InputContextAfterDraw() { syncInputContext(); } };

static void handleNavigationButton();

static void handleGesture(gesture_event_t *event) {
    if (event == NULL) return;
    device::noteUserActivity();

    // Caller holds lvgl_mutex; busy input remains queued.
    // If display was off/dimmed, wake it and consume the gesture
    bool wasDimmed = displayOff || displayManuallyOff || !displayAwake;
    wakeDisplay();

    if (wasDimmed || splashActive) {
        return;  // Consume tap just for waking
    }

    if (simCountdownActive) {
        if (event->type == GESTURE_TAP && ui::bottomAction.contains(event->start_x,event->start_y))
            closeCelebration();
        return;
    }

    if ((showingLandingSeq || showingBlockFoundScreen) && event->type == GESTURE_TAP) {
        const auto action = ui::demoAction(event->start_x, event->start_y);
        if (action == ui::DemoAction::Exit) {
            closeCelebration();
                return;
        }
        if (action == ui::DemoAction::ToggleMute && celebrationHasPlayback && audioHasBlockFoundTrack()) {
            audioSetBlockFoundMuted(!audioBlockFoundMuted());
            if (demoMuteLabel) {
                lv_label_set_text(demoMuteLabel, audioBlockFoundMuted() ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
                lv_obj_center(demoMuteLabel);
            }
                return;
        }
    }


    if (showingLandingSeq || showingBlockFoundScreen) {
        const bool swipe=event->type==GESTURE_SWIPE_LEFT || event->type==GESTURE_SWIPE_RIGHT;
        if (swipe) advanceCelebrationPage(event->type==GESTURE_SWIPE_LEFT);
        return;
    }

    // Provisioning actions run on the owner task; gestures only request them.
    if (inSetupMode) {
        if (event->type == GESTURE_TAP)
            handleSetupAction(ui::setupAction(event->start_x, event->start_y, setupView));
        else if (event->type == GESTURE_SWIPE_RIGHT && setupView == ui::SetupView::Manual)
            handleSetupAction(ui::SetupAction::Qr);
        return;
    }

    if (currentScreenIndex == NAV_SETTINGS && settingsView != ui::SettingsView::List &&
        (event->type == GESTURE_SWIPE_LEFT || event->type == GESTURE_SWIPE_RIGHT)) {
        if (event->type == GESTURE_SWIPE_RIGHT) {
            settingsTappedAction = static_cast<int>(ui::PageAction::Back);
            triggerImmediateRedraw();
        }
        return;
    }

    switch (event->type) {
        case GESTURE_TAP:
            if ((currentScreenIndex==NAV_MINING && ui::miningBlocks.contains(event->start_x,event->start_y)) ||
                (currentScreenIndex==NAV_CLOCK && ui::blockNotice.contains(event->start_x,event->start_y) &&
                 block_record::snapshot().unread)) {
                openSavedBlock();break;
            }
            if (currentScreenIndex == NAV_SETTINGS && lastBuiltScreen == NAV_SETTINGS) {
                settingsTappedAction = static_cast<int>(ui::settingsAction(
                    event->start_x, event->start_y, settingsView));
                if (settingsTappedAction != static_cast<int>(ui::PageAction::None))
                    triggerImmediateRedraw();
            } else if (currentScreenIndex == NAV_CLOCK && lastBuiltScreen == NAV_CLOCK &&
                       ui::clockStatus.contains(event->start_x, event->start_y) && WiFi.status() != WL_CONNECTED) {
                network::requestReconnect();
            }
            break;

        case GESTURE_SWIPE_LEFT:
            showingMiningStats = false;
            currentScreenIndex = (currentScreenIndex + 1) % NUM_MAIN_SCREENS;
            invalidateScreen();
            wakeDisplay();
            triggerImmediateRedraw();
            break;

        case GESTURE_SWIPE_RIGHT:
            showingMiningStats = false;
            currentScreenIndex = (currentScreenIndex - 1 + NUM_MAIN_SCREENS) % NUM_MAIN_SCREENS;
            invalidateScreen();
            wakeDisplay();
            triggerImmediateRedraw();
            break;

        default:
            break;
    }
}

static void serviceTouchActions() {
    if (!lvgl_mutex || xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    if (setupButtonsConsumed.load()) {
        navigationClickPending.store(false);
        touchGesture.consumeUntilRelease();
        touchQueue.clear();
        xSemaphoreGive(lvgl_mutex);
        return;
    }
    if(navigationClickPending.exchange(false))handleNavigationButton();
    syncInputContext();
    const uint32_t context=inputRevision.load();
    lv_obj_t* pressed=nullptr;
    if (touchGesture.pressed() && touchGesture.context()==context && !chargingBootMode) {
        for (const auto& target : touchTargets) if (target.object &&
            target.bounds.contains(touchGesture.startX(),touchGesture.startY())) {
            pressed=target.object; break;
        }
    }
    if (pressed != pressedTouchObject) {
        if (pressedTouchObject) lv_obj_remove_state(pressedTouchObject,LV_STATE_PRESSED);
        pressedTouchObject=pressed;
        if (pressed) lv_obj_add_state(pressed,LV_STATE_PRESSED);
        triggerImmediateRedraw();
    }
    ui::TouchEvent input;
    if (settingsTappedAction==static_cast<int>(ui::PageAction::None) &&
        touchQueue.pop(millis(),context,input) && !chargingBootMode) {
        gesture_event_t event{};
        event.type=input.kind==ui::TouchKind::Tap ? GESTURE_TAP :
            input.kind==ui::TouchKind::Left ? GESTURE_SWIPE_LEFT :
            input.kind==ui::TouchKind::Right ? GESTURE_SWIPE_RIGHT :
            input.kind==ui::TouchKind::Up ? GESTURE_SWIPE_UP : GESTURE_SWIPE_DOWN;
        event.start_x=input.x; event.start_y=input.y;
        event.end_x=input.endX; event.end_y=input.endY; event.duration_ms=input.duration;
        handleGesture(&event);
        syncInputContext();
    }
    xSemaphoreGive(lvgl_mutex);
}

// =============================================================================
// SPLASH CLEANUP
// =============================================================================

void splash_cleanup(void) {
    splashActive = false;
    inSetupMode = false;    // Exit setup mode when entering main screens
}

// =============================================================================
// DISPLAY HARDWARE OBJECTS - Static buffer allocation
// =============================================================================

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RESET, 0, WIDTH, HEIGHT, 22, 0, 0, 0);
static lv_color_t *lvgl_buf = NULL;  // Allocated in PSRAM during init
extern DisplayDriver waveshareAmoled206Driver;

// =============================================================================
// PMU (Power Management) - AXP2101
// =============================================================================

static XPowersAXP2101 pmu;
static bool pmuInitialized = false;

void initPMU() {
    // The AXP2101 gates the entire power-source state machine: isUSBConnected(),
    // isCharging(), getBatteryPercent(), the charging screen, and the auto power
    // profile all depend on pmuInitialized. A single failed begin() (e.g. a
    // transient I2C glitch on a cold boot) would otherwise leave the device
    // permanently convinced it is on battery. Retry a few times before giving up.
    const int PMU_INIT_ATTEMPTS = 3;
    for (int attempt = 1; attempt <= PMU_INIT_ATTEMPTS; ++attempt) {
        // Reset I2C bus to clear any stuck state after crashes
        Wire.end();
        delay(10);
        Wire.begin(PMU_SDA, PMU_SCL);
        delay(50);

        if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL)) {
            pmuInitialized = true;
            pmu.enableBattDetection();
            pmu.enableVbusVoltageMeasure();
            pmu.enableBattVoltageMeasure();
            // Leave time to release the five-second two-button setup shortcut.
            // The independent hardware cutoff remains available for a full hang.
            pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_10S);
            SERIAL_PRINTF("PMU (AXP2101) initialized OK (attempt %d)\n", attempt);
            SERIAL_PRINTF("  Battery connected: %s\n", pmu.isBatteryConnect() ? "YES" : "NO");
            SERIAL_PRINTF("  Battery voltage: %d mV\n", pmu.getBattVoltage());
            SERIAL_PRINTF("  Battery percent: %d%%\n", pmu.getBatteryPercent());
            SERIAL_PRINTF("  Charging: %s\n", pmu.isCharging() ? "YES" : "NO");
            return;
        }

        pmuInitialized = false;
        SERIAL_PRINTF("PMU init FAILED (attempt %d of %d)\n", attempt, PMU_INIT_ATTEMPTS);
        if (attempt < PMU_INIT_ATTEMPTS) delay(100);
    }
    SERIAL_PRINTLN("PMU init FAILED after all retries — assuming battery power");
}

// One synchronized sample/cache/estimator shared by rendering and power checks.
// The I2C mutex protects ALL this state, not just the register transactions.
struct BatterySnapshot {
    battery::Sample sample;
    int percent = -1;
    bool full = false;
};
static battery::Estimator batteryEstimator;
static BatterySnapshot batteryCache;
static uint32_t batteryReadMs = 0;
static bool batteryReadOnce = false;
// Ordinary power-policy callers keep the last known source on a read error.
// Shutdown never uses this fallback: it requires fresh, checked registers.
static std::atomic<bool> lastKnownUSB{false};

static battery::Sample readBatteryRegisters() {
    return battery::readSample([](uint8_t reg) { return pmu.readRegister(reg); });
}

static BatterySnapshot getBatterySnapshot(bool fresh = false) {
    if (!pmuInitialized || !i2cLock(100)) return BatterySnapshot{};
    const uint32_t now = millis();
    if (fresh || !batteryReadOnce || uint32_t(now - batteryReadMs) >= 2000) {
        batteryCache.sample = readBatteryRegisters();
        batteryCache.percent = batteryEstimator.update(batteryCache.sample, now);
        batteryCache.full = batteryEstimator.full();
        batteryReadMs = now;
        batteryReadOnce = true;
        if (batteryCache.sample.statusValid) lastKnownUSB.store(batteryCache.sample.usb);
        diagnostics::batteryRaw(batteryCache.sample.millivolts, batteryCache.sample.gauge,
                                batteryCache.sample.valid() ? 0 : -1, batteryCache.sample.charge);
        diagnostics::batteryDisplayed(batteryCache.percent);
    }
    const BatterySnapshot result = batteryCache;
    i2cUnlock();
    return result;
}

int getBatteryPercent() { return getBatterySnapshot().percent; }
bool isChargeDone() { return getBatterySnapshot().full; }
bool isCharging() { return getBatterySnapshot().sample.charging(); }
bool isUSBConnected() {
    const BatterySnapshot snapshot = getBatterySnapshot();
    return snapshot.sample.statusValid ? snapshot.sample.usb : lastKnownUSB.load();
}

// I2C success and an explicit absent-USB status are both required. A false
// convenience accessor, an input-limited charger or a mutex timeout is not proof.
static bool usbConfirmedAbsent(uint8_t samples = 3, uint16_t gapMs = 40) {
    if (!pmuInitialized || samples == 0) return false;
    for (uint8_t i = 0; i < samples; i++) {
        if (!i2cLock(50)) return false;
        const bool absent = readBatteryRegisters().usbAbsent();
        i2cUnlock();
        if (!absent) return false;
        if (i + 1 < samples) delay(gapMs);
    }
    return true;
}

// =============================================================================
// SMART DIMMING - WITH SCREEN OFF SUPPORT
// =============================================================================

#define DISPLAY_DIM_BRIGHTNESS    40      // 16% brightness - Battery optimized, AMOLED efficient
// Wake brightness now comes from Settings.screenBrightness (user-adjustable via Settings screen)
// Fallback constant used only before settings are loaded
#define DISPLAY_WAKE_BRIGHTNESS_DEFAULT   150

// Low-battery brightness clamp. A near-empty cell sags under the mining/WiFi
// current bursts, and the AMOLED tracks that rail droop as visible flicker
// (most obvious on the Settings screen's large filled rows). Capping brightness
// below this SoC cuts display current → less droop → less flicker, and buys a
// little extra runtime. Updated every few seconds by checkCriticalPower(); read
// (not computed) inside setBrightness() so the hot path stays I2C-free.
#define LOW_BATT_BRIGHTNESS_THRESHOLD  10      // percent — applies on battery only
#define LOW_BATT_BRIGHTNESS_CAP        90      // ~35% of 255
bool g_lowBatteryDim = false;
// Dim/off timeouts. Fixed, not user-adjustable: the screen dims after
// DIM_TIMEOUT_S, then turns fully off SCREEN_OFF_EXTRA_MS later (dim at 10s,
// off at 15s). A static UI left lit on an AMOLED risks permanent burn-in, so
// this is deliberately not exposed as a setting.
static ui::BatteryNotice batteryNotice;
static std::atomic<bool> batteryUserWake{true};

#define DIM_TIMEOUT_S             10UL
#define SCREEN_OFF_EXTRA_MS       5000UL
#define WAKE_TIMEOUT_MS           (DIM_TIMEOUT_S * 1000UL)
#define SCREEN_OFF_TIMEOUT_MS     (DIM_TIMEOUT_S * 1000UL + SCREEN_OFF_EXTRA_MS)

// Sleep the panel without sleeping the CPU: mining and touch wake continue.
// The pinned CO5300 driver supplies the required sleep-in/out command delays.
// Callers serialize display transitions with disp_state_mutex.
static bool displayInHwSleep = false;

static std::atomic<bool> revealAfterRedraw{false};

static void displayHwSleep() {
    if (displayInHwSleep) return;
    setBrightness(0);
    bool locked = (bus_mutex != NULL && xSemaphoreTake(bus_mutex, portMAX_DELAY) == pdTRUE);
    gfx->displayOff();
    displayInHwSleep = true;
    if (locked) xSemaphoreGive(bus_mutex);
}

static void displayHwWake() {
    bool locked = (bus_mutex != NULL && xSemaphoreTake(bus_mutex, portMAX_DELAY) == pdTRUE);
    if (displayInHwSleep) {
        gfx->displayOn();
        displayInHwSleep = false;
        panelRedrawRequested.store(true);
    }
    if (locked) xSemaphoreGive(bus_mutex);
    // Caller restores brightness only after the panel is ready.
}

static void displayDeepSleep() {
    bool held = (disp_state_mutex != NULL &&
                 xSemaphoreTake(disp_state_mutex, portMAX_DELAY) == pdTRUE);
    displayHwSleep();
    if (held) xSemaphoreGive(disp_state_mutex);
}

void flushPendingSettings(bool force);

static void sleepDisplayManually() {
    bool held=disp_state_mutex && xSemaphoreTake(disp_state_mutex,portMAX_DELAY)==pdTRUE;
    displayManuallyOff=true;displayOff=true;displayAwake=false;
    revealAfterRedraw=false;
    displayHwSleep();
    if(held)xSemaphoreGive(disp_state_mutex);
    touchGesture.consumeUntilRelease();touchQueue.clear();
}

// Full power-off, callable from any task (e.g. wManager on portal timeout).
// On battery: display to hardware sleep, then PMU shutdown — PWR button (or
// plugging in USB) boots the device fresh. On USB: restart instead — a PMU
// shutdown with VBUS attached can bounce straight back on, and "plugged in
// while off" means the charging screen in this product anyway.
void powerOffDevice(void) {
    flushPendingSettings(true);
    checkpointMiningStats();
    // Only take the battery (PMU shutdown) path if USB is CONFIRMED absent.
    // A single glitchy "no VBUS" here would PMU-shutdown with the charger
    // attached, which can immediately bounce back on — pick the USB reboot
    // path unless we're sure we're on battery.
    if (pmuInitialized && !usbConfirmedAbsent()) {
        SERIAL_PRINTLN("[POWER] Power-off with USB present — rebooting to charging screen");
        delay(50);
        esp_restart();
    }
    // Not the LVGL task, so guard the display sleep like chargingBootLoop does.
    if (lvgl_mutex != NULL && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        displayDeepSleep();
        xSemaphoreGive(lvgl_mutex);
    } else {
        displayDeepSleep();
    }
    delay(50);
    if (pmuInitialized) {
        diagnostics::checkpoint();
        pmu.shutdown();
    } else {
        // No PMU: deep sleep with PWR-button wake as the fallback "off".
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PWR_BUTTON_PIN, 1);
        delay(50);
        esp_deep_sleep_start();
    }
}

void updateSmartDimming() {
    uint32_t now = millis();

    // NOTE: the critical-battery shutdown used to live here, but this function
    // is only called from the screen-draw paths — which are skipped entirely
    // while the screen is off (i.e. most of the time on the wrist). It now
    // lives in checkCriticalPower(), driven from lv_tick_task, so it runs
    // regardless of screen state.

    if (displayManuallyOff) return;
    if (celebrationProtectsPlayback()) { lastWakeTime=now; return; }

    // Normal dim/sleep timeout — applies on battery AND while charging. A
    // permanently-lit screen on the charger is the worst AMOLED burn-in case (and
    // a nightstand glow), so the screen sleeps when idle on USB too. Waking when
    // the charger is plugged in is handled in checkCriticalPower() (which runs even
    // while the screen is off); touch/button wake it any time.

    if (!displayOff && (now - lastWakeTime > SCREEN_OFF_TIMEOUT_MS)) {
        // Serialize against wakeDisplay so a concurrent wake can't land between
        // the flag set and the brightness/HW-sleep and strand the panel dark.
        bool held = (disp_state_mutex != NULL &&
                     xSemaphoreTake(disp_state_mutex, portMAX_DELAY) == pdTRUE);
        // Re-check under the lock: a wake may have just refreshed lastWakeTime.
        if (!displayOff && (millis() - lastWakeTime > SCREEN_OFF_TIMEOUT_MS)) {
            displayOff = true;
            displayAwake = false;
            setBrightness(0);
            displayHwSleep();  // Panel sleep; mining remains active
        }
        if (held) xSemaphoreGive(disp_state_mutex);
        return;
    }

    if (displayAwake && !displayOff && (now - lastWakeTime > WAKE_TIMEOUT_MS)) {
        bool held = (disp_state_mutex != NULL &&
                     xSemaphoreTake(disp_state_mutex, portMAX_DELAY) == pdTRUE);
        if (displayAwake && !displayOff && (millis() - lastWakeTime > WAKE_TIMEOUT_MS)) {
            displayAwake = false;
            setBrightness(DISPLAY_DIM_BRIGHTNESS);
        }
        if (held) xSemaphoreGive(disp_state_mutex);
    }
}

// Deferred settings persistence. A Settings-screen tap used to call
// nvMem.saveConfig() inline — but that runs on the Monitor task while it holds
// lvgl_mutex, and a SPIFFS write (tens–hundreds of ms) stalls the UI and makes
// handleGesture drop taps/swipes (it gives up on the mutex after 50ms). Mark
// dirty here and flush from lv_tick_task, off the lvgl_mutex, with a short
// debounce so rapid taps coalesce into a single write.
static std::atomic<uint32_t> settingsRevision{0}, savedSettingsRevision{0}, settingsChangedAt{0};
static std::mutex settingsFlushMutex;
static settings::SaveRetry settingsSaveRetry;
static void markSettingsDirty() {
    settingsChangedAt=millis();
    ++settingsRevision;
}
void flushPendingSettings(bool force = false) {
    std::unique_lock<std::mutex> writer(settingsFlushMutex,std::defer_lock);
    if(force)writer.lock();else if(!writer.try_lock())return;
    const uint32_t revision=settingsRevision.load();
    if(revision==savedSettingsRevision.load() || !settingsSaveRetry.due(millis(),settingsChangedAt,force))return;
    // Force callers either own the UI or are powering down. Routine background
    // writes acquire the UI lock only long enough to copy the current settings.
    bool locked=!force && lvgl_mutex && xSemaphoreTake(lvgl_mutex,pdMS_TO_TICKS(5))==pdTRUE;
    if(!force && lvgl_mutex && !locked)return;
    TSettings snapshot=Settings;
    if(locked)xSemaphoreGive(lvgl_mutex);
    const bool ok=nvMem.saveConfig(&snapshot);
    settingsSaveRetry.result(millis(),ok);
    if(ok)savedSettingsRevision=revision; // a newer tap remains dirty
}

// Critical power checks that must run regardless of screen state.
// Called from lv_tick_task (which always runs, even during the setup portal
// and with the screen off). Throttled: the underlying reads hit the PMU over
// I2C. Ordinary display/power getters share a 2s cache; cutoff reads are fresh.
void checkCriticalPower() {
    static uint32_t lastCheck = 0;
    uint32_t now = millis();
    if (now - lastCheck < 5000) return;
    lastCheck = now;

    if (!pmuInitialized) return;

    // Cutoff uses fresh validated hardware evidence, independently of display
    // settling/smoothing. Invalid reads reset the existing two-check policy.
    const BatterySnapshot battery = getBatterySnapshot(true);
    const int battPct = battery.percent;
    const int criticalPct = battery.sample.shutdownPercent();
    if (device::shouldShutdown(criticalPct, criticalPct >= 0 && criticalPct <= 2 && usbConfirmedAbsent())) {
        flushPendingSettings(true);
        checkpointMiningStats();
        diagnostics::checkpoint();
        // Recheck under the bus lock immediately before shutdown: USB may have
        // been inserted during the confirmations or the diagnostics write.
        if (i2cLock(100)) {
            const int finalPct = readBatteryRegisters().shutdownPercent();
            if (finalPct >= 0 && finalPct <= 2) {
                SERIAL_PRINTF("[BATTERY] Critical (%d%%) — shutting down\n", finalPct);
                pmu.clearIrqStatus();
                pmu.shutdown();
                i2cUnlock();
                return;
            }
            i2cUnlock();
        }
    }

    // Wake the screen ONCE when the charger is first plugged in (this runs even
    // from the fully-off state, which updateSmartDimming can't reach). Only on the
    // plug-in transition — NOT continuously while plugged in — so the universal
    // dim/sleep timeout can let the screen sleep on the charger (burn-in / glow).
    // Respect an explicit user screen-off.
    static bool wasUSB = false;
    bool usbNow = isUSBConnected();
    diagnostics::usbState(usbNow);

    // Refresh the low-battery brightness-clamp flag (read by setBrightness). Only
    // on battery — while charging the rail is stiff, so no clamp is needed.
    g_lowBatteryDim = (battPct >= 0 && battPct <= LOW_BATT_BRIGHTNESS_THRESHOLD && !usbNow);

    if (usbNow && !wasUSB && !displayManuallyOff && (displayOff || !displayAwake)) {
        wakeDisplay();
    }
    wasUSB = usbNow;
}

static void wakeDisplayInternal(bool requestNetworkRetry) {
    if(requestNetworkRetry)batteryUserWake=true;
    // Serialize the flag+brightness commit against updateSmartDimming's sleep
    // sequence (see disp_state_mutex). Held only across the display-state part,
    // released before posting the non-blocking Wi-Fi retry request.
    bool held = (disp_state_mutex != NULL &&
                 xSemaphoreTake(disp_state_mutex, portMAX_DELAY) == pdTRUE);
    const bool wasResting = displayOff || displayManuallyOff || !displayAwake;
    displayManuallyOff = false;
    displayAwake = true;
    // Keep the retained frame invisible until current values have been rendered.
    if(wasResting) setBrightness(0);
    displayHwWake();  // Wake CO5300 from hardware sleep (no-op if already awake)
    displayOff = false;
    lastWakeTime = millis();
    if(wasResting) {
        revealAfterRedraw.store(true);panelRedrawRequested.store(true);
        triggerImmediateRedraw();
    } else setBrightness(Settings.screenBrightness > 0 ? Settings.screenBrightness : DISPLAY_WAKE_BRIGHTNESS_DEFAULT);
    if (held) xSemaphoreGive(disp_state_mutex);
    if (requestNetworkRetry && wasResting && !inSetupMode && !chargingBootMode) network::requestReconnect();


}

void wakeDisplay() { wakeDisplayInternal(true); }

bool isDisplayOff() {
    return displayOff;
}

void waveshareDisplayToggle(void) {
    // Ignore screen cycling during charging boot mode
    if (chargingBootMode) return;
    // If display is off or dimmed, wake it
    if (displayManuallyOff || displayOff || !displayAwake) {
        wakeDisplay();
        return;
    }
    // In setup mode there are no main screens to cycle
    if (inSetupMode) return;
    // Cycle to the next main screen
    currentScreenIndex = (currentScreenIndex + 1) % NUM_MAIN_SCREENS;
    invalidateScreen();
    lastWakeTime = millis();  // Reset dim timer on any user interaction
    triggerImmediateRedraw();
}

bool isScreenActive() {
    // Only skip rendering when screen is fully OFF, not when dimmed
    return !displayOff && !displayManuallyOff;
}

// =============================================================================
// BRIGHTNESS CONTROL
// =============================================================================

void setBrightness(uint8_t level) {
    // Low-battery clamp: cap the panel current when the cell is near empty to
    // suppress rail-droop flicker. Never touches level 0 (off/dim-to-black) and
    // only reduces — a user brightness already below the cap is left alone.
    if (level > LOW_BATT_BRIGHTNESS_CAP && g_lowBatteryDim) level = LOW_BATT_BRIGHTNESS_CAP;
    currentBrightness = level;
    // Hold the bus lock so this can't interleave with a frame flush on another task.
    bool locked = (bus_mutex != NULL && xSemaphoreTake(bus_mutex, portMAX_DELAY) == pdTRUE);
    bus->beginWrite();
    bus->writeC8D8(0x51, level);
    bus->endWrite();
    if (locked) xSemaphoreGive(bus_mutex);
}

uint8_t getBrightness() {
    return currentBrightness;
}

// =============================================================================
// HELPERS
// =============================================================================

extern String getTime(void);
extern String getDate(void);
extern unsigned long initialTime;    // From monitor.cpp - NTP epoch time
extern unsigned long mTriggerUpdate; // From monitor.cpp - last NTP update millis
extern bool ntpHasUpdated;          // From monitor.cpp - true only after real NTP response

static bool rtcSynced = false;
static unsigned long rtcSyncGeneration=0;

// Convert a UTC broken-down time to a Unix epoch WITHOUT applying any timezone
// (a portable stand-in for timegm). The hardware RTC stores UTC, so every
// RTC→epoch conversion must interpret its fields as UTC regardless of the local
// zone set via setenv("TZ",...). Storing UTC (not local) makes the offline
// clock immune to a DST change that happens while the watch is powered off.
// Args: full year (e.g. 2026), month 1-12, mday 1-31, hour/min/sec.
static time_t tmToUtcEpoch(int year, int mon, int mday, int hour, int min, int sec) {
    if(!clock_validation::valid(year,mon,mday,hour,min,sec))return 0;
    static const int cumDays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    long y = year;
    long days = (y - 1970) * 365 + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400;
    days += cumDays[(mon - 1) % 12];
    bool leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
    if (mon > 2 && leap) days += 1;
    days += mday - 1;
    return (time_t)days * 86400 + (time_t)hour * 3600 + (time_t)min * 60 + sec;
}

void syncRTCFromNTP() {
    if (!rtcInitialized) return;
    if (!ntpHasUpdated) return;  // Wait for real NTP response, not just RTC seed
    
    // Calculate current epoch time
    unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000;
    unsigned long currentEpoch = initialTime + elapsedTime;
    
    // Convert epoch to struct tm. Use gmtime, NOT localtime: the RTC stores
    // UTC so it stays correct across a DST change while the watch is off. The
    // local zone is applied only when the time is displayed (getDateFromRTC /
    // getTime via localtime_r).
    time_t rawtime = currentEpoch;
    struct tm *timeinfo = gmtime(&rawtime);

    if (timeinfo == NULL) return;
    
    // Fill RTC structure
    RTCDateTime dt;
    dt.second = timeinfo->tm_sec;
    dt.minute = timeinfo->tm_min;
    dt.hour = timeinfo->tm_hour;
    dt.day = timeinfo->tm_mday;
    dt.weekday = timeinfo->tm_wday;  // 0=Sunday, matches PCF85063
    dt.month = timeinfo->tm_mon + 1; // tm_mon is 0-11
    dt.year = timeinfo->tm_year + 1900;
    
    // Retry up to 3 times — I2C writes can fail on noisy boards
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (writeRTC(&dt)) {
            rtcSynced = true;
            rtcSyncGeneration=mTriggerUpdate;
            SERIAL_PRINTF("[RTC] Synced from NTP (attempt %d): %04d-%02d-%02d %02d:%02d:%02d\n",
                attempt, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
            return;
        }
        SERIAL_PRINTF("[RTC] Write failed (attempt %d/3)\n", attempt);
        vTaskDelay(pdMS_TO_TICKS(50));  // Brief pause before retry
    }
    SERIAL_PRINTLN("[RTC] All 3 write attempts failed");
}

// Call this periodically to check if RTC needs sync
void checkRTCSync() {
    if (rtcSynced && rtcSyncGeneration==mTriggerUpdate) return; // Refresh after each real NTP update
    if (!ntpHasUpdated) return;  // Wait for real NTP response
    syncRTCFromNTP();
}

const char* formatUptime(const char* uptime) {
    static char buf[16];
    if (uptime == NULL || uptime[0] == '\0') {
        strcpy(buf, "--");
        return buf;
    }
    
    int days = 0, hours = 0, minutes = 0, seconds = 0;
    if (sscanf(uptime, "%d %d:%d:%d", &days, &hours, &minutes, &seconds) < 4) {
        strncpy(buf, uptime, sizeof(buf) - 1);
        return buf;
    }
    
    if (days >= 7) snprintf(buf, sizeof(buf), "%dd", days);
    else if (days >= 1) snprintf(buf, sizeof(buf), "%dd %dh", days, hours);
    else if (hours >= 1) snprintf(buf, sizeof(buf), "%dh %dm", hours, minutes);
    else if (minutes >= 1) snprintf(buf, sizeof(buf), "%dm %ds", minutes, seconds);
    else snprintf(buf, sizeof(buf), "%ds", seconds);
    
    return buf;
}

String formatTimeClean(const String& time24) {
    int colonPos = time24.indexOf(':');
    if (colonPos < 0) return time24;
    int hour = time24.substring(0, colonPos).toInt();
    String minutes = time24.substring(colonPos + 1, colonPos + 3);

    char buf[12];
    if (Settings.clockFormat == 24) {
        // 24-hour: show leading zero, e.g. "09:30", "14:05"
        snprintf(buf, sizeof(buf), "%02d:%s", hour, minutes.c_str());
    } else {
        // 12-hour: no leading zero, e.g. "9:30", "2:05"
        int h12 = hour;
        if (h12 == 0) h12 = 12;
        else if (h12 > 12) h12 -= 12;
        snprintf(buf, sizeof(buf), "%d:%s", h12, minutes.c_str());
    }
    return String(buf);
}

String getDateFromRTC() {
    if(!initialTime)return "";
    time_t utc=time_t(initialTime)+uint32_t(millis()-mTriggerUpdate)/1000;
    struct tm local;
    if(!localtime_r(&utc,&local))return "";
    const char* days[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    char text[12];snprintf(text,sizeof(text),"%s %d",days[local.tm_wday],local.tm_mday);
    return String(text);
}

lv_color_t getBatteryColor(int percent) {
    if (percent < 0) return THEME_TEXT_MUTED;
    if (percent <= 20) return THEME_RED;
    if (percent <= 50) return THEME_YELLOW;
    return THEME_GREEN;
}

// Dim track color to pair with the arc indicator color
lv_color_t getBatteryTrackColor(int percent) {
    if (percent < 0)   return lv_color_hex(0x1A1A1A);  // muted gray
    if (percent <= 20)  return lv_color_hex(0x3D1010);  // dark red
    if (percent <= 50)  return lv_color_hex(0x3D3010);  // dark yellow
    return lv_color_hex(0x0A3D12);                      // dark green
}

// =============================================================================
// DISPLAY DRIVER CORE
// =============================================================================

static void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    // Hold the bus lock for the whole frame transfer so a concurrent
    // setBrightness()/sleep write on another task can't corrupt it.
    bool locked = (bus_mutex != NULL && xSemaphoreTake(bus_mutex, portMAX_DELAY) == pdTRUE);
    if (!displayInHwSleep)
        gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
    if (locked) xSemaphoreGive(bus_mutex);
    lv_display_flush_ready(disp);
}

// Explicit light sleep only after Battery Saver has stopped real work. The two-
// second timer services battery/USB/watchdogs; it never resumes mining or Wi-Fi.
static bool saverSleepEligible() {
    return device::batterySaverIdle() && network::radioAsleep() &&
        (displayOff || displayManuallyOff) && displayInHwSleep &&
        touchInitialized && pmuInitialized && !inSetupMode && !chargingBootMode &&
        !simCountdownActive && !simCelebrationActive && !showingLandingSeq &&
        !showingBlockFoundScreen && !audioIsPlaying() && g_serialDisabled.load();
}
static bool saverWakeInput() {
    return touchWakeFlag || digitalRead(TOUCH_INT_PIN)==LOW ||
           digitalRead(BOOT_BUTTON_PIN)==LOW || digitalRead(PWR_BUTTON_PIN)==HIGH;
}
static void clearSaverWakeSources() {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable(gpio_num_t(BOOT_BUTTON_PIN));
    gpio_wakeup_disable(gpio_num_t(PWR_BUTTON_PIN));
    gpio_wakeup_disable(gpio_num_t(TOUCH_INT_PIN));
    // Wake configuration overwrites the normal GPIO interrupt type. Restore
    // falling-edge touch detection before re-enabling its ISR (no level storm).
    gpio_set_intr_type(gpio_num_t(TOUCH_INT_PIN),GPIO_INTR_NEGEDGE);
    gpio_intr_enable(gpio_num_t(TOUCH_INT_PIN));
}
static void serviceBatterySaverSleep() {
    static device::SleepBackoff backoff;
    if (!backoff.allowed(millis()) || !saverSleepEligible() || saverWakeInput()) return;
    const auto battery=getBatterySnapshot(true).sample;
    if (!battery.valid() || !battery.usbAbsent() || battery.gauge<=2) return;
    // Settings writes are owned by this tick task. Flush them before requesting
    // the pause, when diagnostics and all other filesystem users can still run.
    flushPendingSettings(true);
    if (!device::requestLightSleep()) return;
    diagnostics::prepareForLightSleep();
    const uint32_t started=millis();
    while (!device::lightSleepReady() && uint32_t(millis()-started)<device::SleepPrepareMs &&
           saverSleepEligible() && !saverWakeInput()) vTaskDelay(pdMS_TO_TICKS(5));
    if (!device::lightSleepReady() || !saverSleepEligible() || saverWakeInput()) {
        const bool timedOut=!device::lightSleepReady() && saverSleepEligible() && !saverWakeInput();
        device::releaseLightSleep();
        if (timedOut) {
            device::recordLightSleep(0,0,ESP_ERR_TIMEOUT);
            backoff.failed(millis());
        }
        return;
    }
    // Same lock order as rendering/wake. Nonblocking acquisition avoids sleeping
    // while a hardware transaction or a late audio prewarm is still in progress.
    const bool lv=lvgl_mutex && xSemaphoreTake(lvgl_mutex,0)==pdTRUE;
    const bool state=lv && disp_state_mutex && xSemaphoreTake(disp_state_mutex,0)==pdTRUE;
    const bool bus=state && bus_mutex && xSemaphoreTake(bus_mutex,0)==pdTRUE;
    const bool i2c=bus && i2c_mutex && xSemaphoreTake(i2c_mutex,0)==pdTRUE;
    const bool audio=i2c && audioPrepareLightSleep();
    bool slept=false;
    int cause=0;
    esp_err_t error=ESP_OK;
    uint32_t duration=0;
    if (audio && saverSleepEligible() && !saverWakeInput()) {
        // Revalidate VBUS with the I2C lock held immediately before entry.
        const auto fresh=readBatteryRegisters();
        if (fresh.valid() && fresh.usbAbsent() && fresh.gauge>2) {
            gpio_intr_disable(gpio_num_t(TOUCH_INT_PIN));
            error=gpio_wakeup_enable(gpio_num_t(BOOT_BUTTON_PIN),GPIO_INTR_LOW_LEVEL);
            if (error==ESP_OK) error=gpio_wakeup_enable(gpio_num_t(PWR_BUTTON_PIN),GPIO_INTR_HIGH_LEVEL);
            if (error==ESP_OK) error=gpio_wakeup_enable(gpio_num_t(TOUCH_INT_PIN),GPIO_INTR_LOW_LEVEL);
            if (error==ESP_OK) error=esp_sleep_enable_gpio_wakeup();
            if (error==ESP_OK) error=esp_sleep_enable_timer_wakeup(uint64_t(device::SleepSliceMs)*1000);
            if (error==ESP_OK && !saverWakeInput()) {
                // Retain default flash/PSRAM power policy and all wake circuitry.
                // App tasks are parked at checkpoints; ESP-IDF handles both CPUs.
                const int64_t before=esp_timer_get_time();
                error=esp_light_sleep_start();
                duration=uint32_t((esp_timer_get_time()-before)/1000);
                cause=int(esp_sleep_get_wakeup_cause());
                slept=true;
                lv_tick_inc(duration); // Include sleep once; tick loop subtracts it.
                accountedSleepMs += duration;
            }
            clearSaverWakeSources();
        }
    }
    if (audio) audioFinishLightSleep();
    if (i2c) xSemaphoreGive(i2c_mutex);
    if (bus) xSemaphoreGive(bus_mutex);
    if (state) xSemaphoreGive(disp_state_mutex);
    if (lv) xSemaphoreGive(lvgl_mutex);
    // A GPIO wake is activity even if its short interrupt cleared before return.
    // Consume a PWR/BOOT press while the display is still asleep so its normal
    // handler wakes, rather than accidentally turning off the now-awake watch.
    const bool input=(slept && cause==ESP_SLEEP_WAKEUP_GPIO) || saverWakeInput();
    if (input) {
        device::noteUserActivity();
        checkWaveshareButtons();
        if (digitalRead(BOOT_BUTTON_PIN)==LOW) bootWakeConsumed=true;
        touchWakeFlag=false;
        wakeDisplay();
    }
    device::releaseLightSleep();
    if (slept || error!=ESP_OK) {
        if (error==ESP_OK && !input && (cause!=ESP_SLEEP_WAKEUP_TIMER || duration<50))
            error=ESP_ERR_INVALID_STATE; // Don't spin on a stuck/unexpected wake source.
        device::recordLightSleep(duration,cause,int(error));
        if (error==ESP_OK) backoff.succeeded();
        else backoff.failed(millis());
    }
}

void lv_tick_task(void *arg) {
    touchTaskHandle=xTaskGetCurrentTaskHandle();
    bool wasScreenOff = false;
    ui::WifiDisplayWake wifiDisplayWake;
    uint32_t previousTick=millis();

    while (1) {
        // The render task skips dark screens. Consume Saver's page reset here
        // before wake input, including when the watch never enters light sleep.
        if (lvgl_mutex && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            applyBatterySaverScreenReset();
            // Smooth the one-time reveal using the existing awake tick. Only
            // wake the render task; LVGL stays owned by its locked render path.
            if (splashActive && !displayOff && !displayManuallyOff)
                needsImmediateRedraw.store(true);
            xSemaphoreGive(lvgl_mutex);
        }
        // When display is off, slow this task from 50Hz to 20Hz to save
        // CPU overhead while keeping button wake responsive (50ms polling).
        // Touch wake uses GPIO interrupt instead.
        bool screenOff = displayOff || displayManuallyOff;
        uint32_t tickInterval = screenOff ? 50 : 20;

        // Manage touch interrupt for wake-on-touch:
        // - Attach interrupt when screen turns off (zero-power touch detection)
        // - Detach when screen turns on (normal I2C polling takes over)
        if (screenOff && !wasScreenOff && touchInitialized) {
            touchGesture.consumeUntilRelease();
            touchQueue.clear();
            touchWakeFlag = false;
            attachInterrupt(digitalPinToInterrupt(TOUCH_INT_PIN), touchWakeISR, FALLING);
            if (digitalRead(TOUCH_INT_PIN)==LOW) touchWakeFlag=true;
        } else if (!screenOff && wasScreenOff) {
            detachInterrupt(digitalPinToInterrupt(TOUCH_INT_PIN));
            touchWakeFlag = false;
        }
        wasScreenOff = screenOff;

        // Wake-on-touch: if the FT3168 INT pin fired while screen was off,
        // wake the display immediately — no I2C read needed.
        if (screenOff && touchWakeFlag) {
            touchGesture.consumeUntilRelease();
            touchQueue.clear();
            device::noteUserActivity();
            touchWakeFlag = false;
            wakeDisplay();
            // Don't detach here — the state transition above will handle it
            // on the next loop iteration when screenOff becomes false.
        }

        const uint32_t tickNow=millis();
        lv_tick_inc(uint32_t(tickNow-previousTick)-accountedSleepMs);
        accountedSleepMs=0;
        previousTick=tickNow;

        // Poll buttons — runs as an independent FreeRTOS task so buttons work
        // even when WiFiManager blocks loop() during the setup portal
        checkWaveshareButtons();

        // Critical-battery shutdown + USB-plug wake. Runs here (not in the
        // draw path) so it works with the screen off — i.e. on the wrist.
        checkCriticalPower();

        // Connection notifications wake only the panel; never record user activity
        // or request another network retry. The Wi-Fi owner handles the one-shot Saver extension.
        // Snapshot the event before checking eligibility. If GOT_IP arrives
        // during these checks, its new event remains pending for the next tick
        // instead of being consumed with the previous disconnected state.
        const uint32_t wifiConnections = network::successfulConnections();
        const bool canWakeForWifi = !inSetupMode && !chargingBootMode &&
            !displayManuallyOff && !device::batterySaverIdle() &&
            !network::radioAsleep() && network::wifiStage()==network::WifiStage::Connected &&
            (displayOff || !displayAwake);
        if (wifiDisplayWake.update(wifiConnections, millis(), canWakeForWifi)) {
            wakeDisplayInternal(false);
        }

        // Persist any pending Settings change off the lvgl_mutex (debounced).
        flushPendingSettings();

        // Poll touch only when display is on — saves I2C bus power (~2-3 mA)
        // when screen is off. Wake-on-touch interrupt handles the off state.
        if (!screenOff && touchInitialized) {
            touch_point_t point = {0};
            if (touch_read(&point)) {
                if (point.touched) { device::noteUserActivity(); lastWakeTime=millis(); }
                if (point.touched && (!displayAwake || revealAfterRedraw.load())) {
                    touchGesture.consumeUntilRelease();
                    touchQueue.clear();
                    if (!displayAwake) wakeDisplay();
                } else {
                    ui::PageRect bounds{};
                    const bool bounded=touchBounds(point.x,point.y,bounds);
                    touchQueue.push(touchGesture.update(point.touched,point.x,point.y,millis(),
                        inputRevision.load(),bounded ? &bounds : nullptr));
                }
            }
        }

        serviceTouchActions();
        // Sleep service advances LVGL by sleep duration itself. Account its awake
        // preparation work next loop, subtract only the recorded sleep duration.
        serviceBatterySaverSleep();
        if (revealAfterRedraw.load()) { touchGesture.consumeUntilRelease(); touchQueue.clear(); }
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(tickInterval));
    }
}

// =============================================================================
// TOUCH UPDATE - Tick task handles touch at 50ms intervals (20Hz)
// This exists for API compatibility with main loop
// =============================================================================

void updateTouchInput(void) {
    // Touch is polled in lv_tick_task every 20ms (50Hz)
    // Main loop only runs every 50ms, so we handle touch in tick task instead
}

// =============================================================================
// BATTERY INDICATOR (Large with percentage inside)
// =============================================================================

void createBatteryIndicatorLarge(int x, int y) {
    int percent = getBatteryPercent();
    bool charging = isCharging();
    lv_color_t color = getBatteryColor(percent);
    
    // Battery outline - larger to fit text inside
    lv_obj_t *outline = lv_obj_create(lv_scr_act());
    lv_obj_set_size(outline, 70, 32);
    lv_obj_set_pos(outline, x, y);
    lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(outline, BATTERY_OUTLINE_COLOR, 0);
    lv_obj_set_style_border_width(outline, 3, 0);
    lv_obj_set_style_radius(outline, 8, 0);
    
    // Battery tip
    lv_obj_t *tip = lv_obj_create(lv_scr_act());
    lv_obj_set_size(tip, 6, 16);
    lv_obj_set_pos(tip, x + 70, y + 8);
    lv_obj_set_style_bg_color(tip, BATTERY_OUTLINE_COLOR, 0);
    lv_obj_set_style_border_width(tip, 0, 0);
    lv_obj_set_style_radius(tip, 3, 0);
    
    // Fill bar - green when charging, otherwise based on level
    if (percent > 0) {
        int fillWidth = (percent * 60) / 100;
        if (fillWidth < 6) fillWidth = 6;
        lv_obj_t *fill = lv_obj_create(lv_scr_act());
        lv_obj_set_size(fill, fillWidth, 22);
        lv_obj_set_pos(fill, x + 5, y + 5);
        lv_obj_set_style_bg_color(fill, charging ? THEME_GREEN : color, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_70, 0);  // 70% opacity (original translucent style)
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 4, 0);
    }
    
    // Percentage text centered
    char battText[8];
    snprintf(battText, sizeof(battText), percent >= 0 ? "%d" : "--", percent);
    lv_obj_t *battLabel = lv_label_create(lv_scr_act());
    lv_label_set_text(battLabel, battText);
    lv_obj_set_style_text_font(battLabel, &inter_24, 0);
    lv_obj_set_style_text_color(battLabel, THEME_TEXT_WHITE, 0);
    lv_obj_update_layout(battLabel);
    lv_obj_set_pos(battLabel, x + (70 - lv_obj_get_width(battLabel)) / 2, y + (32 - lv_obj_get_height(battLabel)) / 2);

}

// =============================================================================
// PAGE DOTS - Active dot is orange, inactive dots are dark gray
// =============================================================================

void createPageDots(int activePage) {
    int dotSize = 18, dotGap = 15;  // 50% larger dots (was 12)
    int totalWidth = (NUM_MAIN_SCREENS * dotSize) + (dotGap * (NUM_MAIN_SCREENS - 1));
    int startX = (WIDTH - totalWidth) / 2;
    int y = HEIGHT - SAFE_BOTTOM + 4;

    for (int i = 0; i < NUM_MAIN_SCREENS; i++) {
        lv_obj_t *dot = lv_obj_create(lv_scr_act());
        // Decorative circles must not inherit container padding or scrollbars.
        lv_obj_remove_style_all(dot);
        lv_obj_clear_flag(dot, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_size(dot, dotSize, dotSize);
        lv_obj_set_style_radius(dot, dotSize / 2, 0);

        // Active dot is orange, inactive dots are dark gray
        if (i == activePage) {
            lv_obj_set_style_bg_color(dot, THEME_ORANGE, 0);
        } else {
            lv_obj_set_style_bg_color(dot, THEME_TEXT_DARK, 0);
        }

        lv_obj_set_pos(dot, startX + (i * (dotSize + dotGap)), y);
    }
}

// =============================================================================
// INIT
// =============================================================================

void waveshareDisplay_Init(void) {
    delay(1000);
    SERIAL_PRINTLN("=== SOLOMINER INIT ===");
    
    lvgl_mutex = xSemaphoreCreateMutex();
    bus_mutex = xSemaphoreCreateMutex();
    i2c_mutex = xSemaphoreCreateMutex();
    disp_state_mutex = xSemaphoreCreateMutex();
    initPMU();
    
    if (!gfx->begin()) { SERIAL_PRINTLN("GFX FAIL"); return; }
    
    // Fill screen black immediately to prevent flash of garbage/random colors
    gfx->fillScreen(0x0000);
    
    setBrightness(Settings.screenBrightness > 0 ? Settings.screenBrightness : DISPLAY_WAKE_BRIGHTNESS_DEFAULT);

    lv_init();
    // Allocate a full-screen framebuffer from PSRAM (8MB available on this board).
    // This lets LVGL render and flush the entire frame in one pass (FULL mode),
    // eliminating the top-to-bottom wave artifact caused by partial strip updates.
    lvgl_buf = (lv_color_t *)ps_malloc(WIDTH * HEIGHT * sizeof(lv_color_t));
    lv_display_t *disp = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_buffers(disp, lvgl_buf, NULL, WIDTH * HEIGHT * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, my_disp_flush);
    // 16 KB stack: this task runs the gesture engine, touch I2C, button handling,
    // and wakeDisplay() (display wake and brightness updates
    // chain). 8 KB was too tight and risked a stack-overflow panic.
    xTaskCreatePinnedToCore(lv_tick_task, "LVGL Tick", 16384, NULL, 3, NULL, 1);
    
    if (touch_init()) {
        SERIAL_PRINTLN("Touch initialized - gesture processing enabled");
        // Note: We don't register LVGL touch input because we use custom gesture handling
        // This avoids double I2C reads and improves responsiveness
    }
    
    initRTC();

    // Seed system time from the hardware RTC so the clock is accurate before NTP.
    // Without this, initialTime=0 and the display shows 1970 until WiFi connects.
    if (rtcInitialized) {
        RTCDateTime rtcNow;
        if (readRTC(&rtcNow) && rtcNow.year >= 2024) {
            // RTC holds UTC → interpret its fields as UTC (not local). initialTime
            // is a UTC epoch; getTime/getDate apply the zone+DST at display time.
            time_t epoch = tmToUtcEpoch(rtcNow.year, rtcNow.month, rtcNow.day,
                                        rtcNow.hour, rtcNow.minute, rtcNow.second);
            if (epoch > 0) {
                initialTime    = (unsigned long)epoch;
                mTriggerUpdate = millis();
                SERIAL_PRINTF("[RTC] Boot time seeded (UTC): %04d-%02d-%02d %02d:%02d:%02d\n",
                    rtcNow.year, rtcNow.month, rtcNow.day,
                    rtcNow.hour, rtcNow.minute, rtcNow.second);
            }
        }
    }

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PWR_BUTTON_PIN, INPUT);  // External pull-down; INPUT_PULLUP fights it and causes unreliable reads on battery

    SERIAL_PRINTLN("Display init complete!");
}

void waveshareDisplay_AlternateScreenState(void) {}
void waveshareDisplay_AlternateRotation(void) {}

// =============================================================================
// SPLASH SCREEN
// =============================================================================

void waveshareDisplay_LoadingScreen(void) {
    if (!lvgl_mutex || xSemaphoreTake(lvgl_mutex, portMAX_DELAY) != pdTRUE) return;
    if (!lv_scr_act()) { xSemaphoreGive(lvgl_mutex); return; }
    lv_obj_clean(lv_scr_act());
    invalidateScreen();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    // Match Clock so the stars stay in place when the usable UI appears.
    clock_stars::create(lv_scr_act());

    lv_obj_t *logo = lv_image_create(lv_scr_act());
    if (logo) {
        // Keep the static stars visible through the original alpha artwork.
        lv_obj_set_style_bg_opa(logo, LV_OPA_TRANSP, 0);
        lv_image_set_src(logo, &Astronaut_200x200);
        lv_image_set_scale(logo, 400);
        lv_obj_align(logo, LV_ALIGN_CENTER, 0, 0);
        // Zero duration makes this a delayed instant reveal, without a fade.
        // Deleting the image cancels the pending reveal automatically.
        lv_obj_fade_in(logo, 0, SPLASH_STARS_MS);
        // Keep the astronaut until the next screen replaces the splash. No
        // separate removal callback or empty beat before Clock / Setup.
    }

    splashStartMs = millis();
    splashActive = true;
    currentScreenIndex = NAV_CLOCK;
    syncInputContext();
    lv_refr_now(NULL);
    xSemaphoreGive(lvgl_mutex);
    SERIAL_PRINTLN("Splash started");
}

// =============================================================================
// CHARGING BOOT SCREEN - Shown when device powers on via USB while "off"
// User presses the lower (PWR) button to start mining
// =============================================================================

// One layout for both the initial wait and the battery level. Updating the
// reading never rebuilds the screen or resets its inactivity timer.
static lv_obj_t *chargingArc = NULL;
static lv_obj_t *chargingReadingLabel = NULL;
static lv_obj_t *chargingPctLabel = NULL;
static lv_obj_t *chargingStatus = NULL;
static lv_obj_t *chargingHint = NULL;
static bool chargingScreenBuilt = false;
static int chargingTargetPct = -1;

static void setChargingPctText(lv_obj_t *lbl, int battPct) {
    if (!lbl) return;
    if (battPct >= 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", battPct);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &inter_72, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    } else {
        lv_label_set_text(lbl, "");
    }
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -40);
}

static void animateChargingArc(void *obj, int32_t value) {
    lv_obj_t *arc = static_cast<lv_obj_t *>(obj);
    lv_arc_set_value(arc, value);
    // The AMOLED driver flushes a full framebuffer. Preserve the static stars
    // and labels around the small animated region on every reveal frame.
    lv_obj_invalidate(lv_obj_get_screen(arc));
}


// The moving segment indicates an unavailable reading, not charge progress.
// LVGL removes this object-owned animation when the screen is cleaned.
static void rotateReadingArc(void *obj, int32_t angle) {
    lv_obj_t *arc = static_cast<lv_obj_t *>(obj);
    lv_arc_set_rotation(arc, angle);
    lv_obj_invalidate(lv_obj_get_screen(arc));
}

static void updateChargingScreen(const BatterySnapshot &battery) {
    if (!chargingArc || !chargingReadingLabel || !chargingPctLabel) return;

    const int battPct = battery.percent >= 0 && battery.percent <= 100 ? battery.percent : -1;
    const bool waiting = battPct < 0;
    batteryNotice.sample(battPct,battery.sample.usb,millis());
    const bool unavailable=batteryNotice.unavailable(millis());
    if (waiting) {
        lv_label_set_text(chargingReadingLabel,unavailable ? "Battery reading\nunavailable" : "Reading\nbattery level...");
        lv_obj_clear_flag(chargingReadingLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chargingPctLabel, LV_OBJ_FLAG_HIDDEN);
        lv_anim_delete(chargingArc, animateChargingArc);
        lv_arc_set_value(chargingArc, 16);
        lv_obj_set_style_arc_color(chargingArc, lv_color_hex(0xFF982C), LV_PART_INDICATOR);
        if(unavailable) {
            lv_anim_delete(chargingArc,rotateReadingArc);
            lv_arc_set_value(chargingArc,0);
        } else if (!lv_anim_get(chargingArc, rotateReadingArc)) {
            lv_anim_t spin;
            lv_anim_init(&spin);
            lv_anim_set_var(&spin, chargingArc);
            lv_anim_set_exec_cb(&spin, rotateReadingArc);
            lv_anim_set_values(&spin, 270, 630);
            lv_anim_set_duration(&spin, 2200);
            lv_anim_set_repeat_count(&spin, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&spin);
        }
    } else {
        lv_anim_delete(chargingArc, rotateReadingArc);
        lv_arc_set_rotation(chargingArc, 270);
        if (chargingTargetPct < 0) lv_arc_set_value(chargingArc, 0);
        lv_obj_set_style_arc_color(chargingArc, THEME_GREEN, LV_PART_INDICATOR);
        lv_obj_add_flag(chargingReadingLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chargingPctLabel, LV_OBJ_FLAG_HIDDEN);
        // Animate only a real, validated level. The number itself never counts
        // up through invented readings. Stop the indeterminate reading arc
        // before revealing the validated level.
        if (battPct != chargingTargetPct) {
            lv_anim_delete(chargingArc, animateChargingArc);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, chargingArc);
            lv_anim_set_exec_cb(&a, animateChargingArc);
            lv_anim_set_values(&a, lv_arc_get_value(chargingArc), battPct);
            lv_anim_set_duration(&a, 350);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
        }
    }
    chargingTargetPct = battPct;
    lv_obj_set_style_arc_opa(chargingArc, LV_OPA_COVER, LV_PART_INDICATOR);
    setChargingPctText(chargingPctLabel, battPct);
    if (chargingStatus) {
        if (waiting) lv_obj_add_flag(chargingStatus, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(chargingStatus, LV_OBJ_FLAG_HIDDEN);
        // USB presence alone does not prove that the cell is charging.
        const char *text = battery.full && battPct == 100 ? "Charged" :
                           battery.sample.charging() ? "Charging" : "Power connected";
        lv_label_set_text(chargingStatus, text);
        lv_obj_align(chargingStatus, LV_ALIGN_CENTER, 0, 30);
    }
    lv_obj_invalidate(lv_obj_get_screen(chargingArc));
}

static void buildChargingScreen(const BatterySnapshot &battery) {
    lv_obj_t *scr = lv_scr_act();
    if (!scr) return;
    lv_obj_clean(scr);
    chargingScreenBuilt = false;
    chargingArc = NULL;
    chargingReadingLabel = NULL;
    chargingPctLabel = NULL;
    chargingStatus = NULL;
    chargingHint = NULL;
    chargingTargetPct = -1;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    clock_stars::create(scr, &clock_stars::chargingField);

    chargingArc = lv_arc_create(scr);
    if (!chargingArc) { SERIAL_PRINTLN("[LVGL] chargingArc alloc failed"); return; }
    lv_obj_set_size(chargingArc, 320, 320);
    lv_obj_align(chargingArc, LV_ALIGN_CENTER, 0, -30);
    lv_arc_set_rotation(chargingArc, 270);
    lv_arc_set_bg_angles(chargingArc, 0, 360);
    lv_arc_set_range(chargingArc, 0, 100);
    lv_obj_remove_flag(chargingArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(chargingArc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(chargingArc, lv_color_hex(0x20262C), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(chargingArc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(chargingArc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(chargingArc, THEME_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(chargingArc, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(chargingArc, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(chargingArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_arc_set_value(chargingArc, 0);

    chargingReadingLabel = lv_label_create(scr);
    if (!chargingReadingLabel) { SERIAL_PRINTLN("[LVGL] chargingReadingLabel alloc failed"); return; }
    lv_obj_set_width(chargingReadingLabel, 250);
    lv_obj_set_style_text_font(chargingReadingLabel, &inter_28, 0);
    lv_obj_set_style_text_color(chargingReadingLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(chargingReadingLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(chargingReadingLabel, 6, 0);
    lv_label_set_text(chargingReadingLabel, "Reading\nbattery level...");
    lv_obj_align(chargingReadingLabel, LV_ALIGN_CENTER, 0, -30);

    chargingPctLabel = lv_label_create(scr);
    if (!chargingPctLabel) { SERIAL_PRINTLN("[LVGL] chargingPctLabel alloc failed"); return; }

    chargingStatus = lv_label_create(scr);
    if (chargingStatus) {
        lv_obj_set_style_text_font(chargingStatus, &inter_28, 0);
        lv_obj_set_style_text_color(chargingStatus, THEME_TEXT_MUTED, 0);
    }

    lv_obj_t *hint = lv_label_create(scr);
    chargingHint = hint;
    if (hint) {
        lv_label_set_text(hint, "Press lower button to start");
        lv_obj_set_style_text_font(hint, &inter_28, 0);
        lv_obj_set_style_text_color(hint, THEME_TEXT_MUTED, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -SAFE_BOTTOM - 10);
    }

    updateChargingScreen(battery);
    setBrightness(Settings.screenBrightness > 0 ? Settings.screenBrightness : DISPLAY_WAKE_BRIGHTNESS_DEFAULT);
    lastWakeTime = millis();
    chargingScreenBuilt = true;
    lv_refr_now(NULL);
}

// Charging boot runs before the normal animation task. Complete its wake/reveal
// here too: wakeDisplayInternal deliberately leaves brightness at zero until a
// current frame has reached the panel.
static void refreshChargingDisplay(const BatterySnapshot &battery) {
    if (displayOff || displayManuallyOff) return;
    if (panelRedrawRequested.exchange(false)) {
        updateChargingScreen(battery);
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);
        if (revealAfterRedraw.exchange(false)) {
            bool held = disp_state_mutex != NULL &&
                        xSemaphoreTake(disp_state_mutex, portMAX_DELAY) == pdTRUE;
            if (!displayOff && !displayManuallyOff && displayAwake)
                setBrightness(Settings.screenBrightness > 0 ? Settings.screenBrightness : DISPLAY_WAKE_BRIGHTNESS_DEFAULT);
            if (held) xSemaphoreGive(disp_state_mutex);
        }
    }
    lv_timer_handler();
}

// Blocking loop for charging boot mode — called from setup() before WiFi/mining init.
// Draws a charging screen; lower Power starts the watch when the display is awake.
// A dark-screen button press only wakes it; upper BOOT never starts the miner.
// lv_tick_task is already running (handles button polling on separate FreeRTOS task).
void chargingBootLoop(void) {
    SERIAL_PRINTLN("[CHARGE] Entering charging boot mode");
    chargingScreenBuilt = false;
    int lastPct = -999;  // Force initial draw
    // Ensure full brightness — we're on USB power, no need to dim
    displayAwake = true;
    setBrightness(Settings.screenBrightness > 0 ? Settings.screenBrightness : DISPLAY_WAKE_BRIGHTNESS_DEFAULT);

    unsigned long lastUpdate = 0;  // Non-static so it resets on each entry

    // The shared estimator handles startup and USB settling. Unknown readings
    // show the reading text and indeterminate arc; never reset the estimator.
    // LVGL is not pumped while the display sleeps, so the arc does not render
    // in the background.
    while (chargingBootMode) {
        const BatterySnapshot battery = getBatterySnapshot();
        const int pct = battery.percent;
        unsigned long now = millis();

        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(navigationClickPending.exchange(false) && !setupButtonsConsumed.load())handleNavigationButton();
            if (!chargingScreenBuilt) {
                // Build once; subsequent readings update the existing objects.
                buildChargingScreen(battery);
                lastPct = pct;
                lastUpdate = now;
            } else if (!displayOff && (pct != lastPct || (now - lastUpdate >= 2000))) {
                // Read percentage and charger status from the same snapshot.
                updateChargingScreen(battery);
                lastPct = pct;
                lastUpdate = now;
            }
            // Pump LVGL only when display is on — no point rendering to a
            // sleeping display, and it prevents SPI bus contention between
            // the flush callback and brightness/sleep commands.
            refreshChargingDisplay(battery);

            // Sleep the screen after a short idle window — a static, bright
            // AMOLED on the charger is the worst burn-in case, and it glows on
            // a nightstand. Touch (wake interrupt in lv_tick_task) or a PWR
            // press wakes it for another peek; an awake PWR tap starts the miner.
            #define CHARGE_SCREEN_SLEEP_MS 30000UL
            // Building or waking the screen can advance lastWakeTime beyond
            // the loop's earlier timestamp. Read time here to avoid underflow.
            bool held = (disp_state_mutex != NULL &&
                         xSemaphoreTake(disp_state_mutex, portMAX_DELAY) == pdTRUE);
            if (!displayOff && (millis() - lastWakeTime >= CHARGE_SCREEN_SLEEP_MS)) {
                SERIAL_PRINTLN("[CHARGE] Screen sleeping — touch or PWR to wake");
                displayOff = true;
                displayAwake = false;
                displayHwSleep();
            }
            if (held) xSemaphoreGive(disp_state_mutex);

            xSemaphoreGive(lvgl_mutex);
        }

        // If USB power removed while in charging screen, shut down. Require a
        // CONFIRMED absence (usbConfirmedAbsent samples several times) — a lone
        // glitchy/contended VBUS read must not power the watch off mid-charge.
        // Use VBUS not isCharging() — charging stops when the battery is full
        // but USB is still connected; we only shut down when actually unplugged.
        if (pmuInitialized && usbConfirmedAbsent()) {
            SERIAL_PRINTLN("[CHARGE] USB disconnected — powering off via PMU");
            if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                displayDeepSleep();
                xSemaphoreGive(lvgl_mutex);
            }
            delay(50);
            diagnostics::checkpoint();
            pmu.shutdown();  // Full power off — re-plugging USB triggers fresh boot
        }

        vTaskDelay(pdMS_TO_TICKS(16));  // Keep charging-screen input responsive
    }

    SERIAL_PRINTLN("[CHARGE] Power button pressed — continuing normal boot");
    chargingScreenBuilt = false;
    chargingArc = NULL;
    chargingReadingLabel = NULL;
    chargingPctLabel = NULL;
    chargingStatus = NULL;
    chargingHint = NULL;
    chargingTargetPct = -1;
    // Reset screen index to first screen so normal boot starts clean
    currentScreenIndex = NAV_CLOCK;
    invalidateScreen();
    // Keep the last charging frame until LoadingScreen replaces it immediately.
}

// Shared setup-connect state (declared here so the QR screen can show an error
// banner; the connecting-screen timeout logic lives further below).

static void drawSetupQRScreen(void) {
    if (!lv_scr_act()) return;
    invalidateScreen();
    if (setupConnectFailed) {
        setupView = ui::SetupView::Failed;
        pageMessage("Check Wi-Fi", network::wifiProblem()==network::WifiProblem::NotFound
            ? "Network not found.\nTurn on your Wi-Fi or hotspot."
            : network::wifiProblem()==network::WifiProblem::Credentials
            ? "Could not join.\nCheck your Wi-Fi password."
            : "Could not join.\nCheck your network or hotspot.", true);
        pageAction(ui::setupAlternate, "Edit Wi-Fi", false);
        pageAction(ui::setupExit, "Exit setup", false);
    } else {
        setupView = ui::SetupView::Qr;
        clearPage();
        pageText("Scan to connect", &inter_28_semi_bold, THEME_TEXT_WHITE, 31);
        char payload[256];
        if (!ui::setupQrPayload(payload, sizeof(payload), setupNetworkName, DEFAULT_WIFIPW) || !pageQr(payload, 207))
            pageText("Use manual setup", &inter_28, THEME_TEXT_MUTED, 185);
        pageText("Setup didn't open?", &inter_20, THEME_TEXT_MUTED, 345);
        pageText("Visit 192.168.4.1", &inter_20, THEME_TEXT_WHITE, 371);
        pageAction(ui::setupAlternate, "Manual", false);
        pageAction(ui::setupExit, "Exit setup", false);
    }
    lv_refr_now(NULL);
}

static void drawSetupDetailsScreen(void) {
    if (!lv_scr_act()) return;
    invalidateScreen();
    setupView = ui::SetupView::Manual;
    clearPage();
    buildDetailHeader("Manual setup");
    pageText("Join Wi-Fi", &inter_20, THEME_TEXT_MUTED, 108);
    auto* name = pageText(sanitizeForFont(String(setupNetworkName)).c_str(), &inter_28, THEME_TEXT_WHITE, 141);
    if (name) { lv_obj_set_height(name, 40); lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL); }
    pageText("Password", &inter_20, THEME_TEXT_MUTED, 198);
    pageText(DEFAULT_WIFIPW, &inter_28, THEME_TEXT_WHITE, 230);
    pageText("Then open in your browser", &inter_20, THEME_TEXT_MUTED, 288);
    pageText("192.168.4.1", &inter_28, THEME_TEXT_WHITE, 321);
    pageAction(ui::setupExitWide, "Exit setup", false);
    lv_refr_now(NULL);
}

static void handleSetupAction(ui::SetupAction action) {
    if (action == ui::SetupAction::None || setupView == ui::SetupView::Leaving) return;
    if (action == ui::SetupAction::Exit) {
        setupView = ui::SetupView::Leaving;
        setupRedrawRequested = false;
        pageMessage("Leaving setup", "Returning to your watch");
        requestSetupExit();
    } else {
        setupConnectFailed = false;
        setupShowingDetails = action == ui::SetupAction::Manual;
        setupRedrawRequested = true;
    }
    triggerImmediateRedraw();
}

void toggleSetupScreen(void) {
    setupShowingDetails = !setupShowingDetails;
    setupRedrawRequested = true;  // Dispatcher handles the actual LVGL draw
}

void waveshareDisplay_SetupScreen(void) {
    splash_cleanup();
    inSetupMode = true;
    setupShowingDetails = false;  // Always start with QR code
    drawSetupQRScreen();
}

// =============================================================================
// SETUP → CONNECTING SCREEN (shown after portal form submitted)
// =============================================================================

static bool setupConnectingDrawn = false;
static uint32_t setupConnectingAttempt = 0;
static unsigned long setupConnectingStart = 0;
// setupConnectFailed is declared above (used by drawSetupQRScreen).
#define SETUP_CONNECT_TIMEOUT_MS 40000           // give up "connecting" after 40s → likely bad WiFi password

static void drawSetupConnectingScreen(const WifiTargetSnapshot& target) {
    if (!lv_scr_act()) return;
    invalidateScreen();
    setupView = ui::SetupView::Connecting;
    clearPage();
    pageText("Connecting", &inter_36_bold, THEME_TEXT_WHITE, 178);
    auto* name = pageText(sanitizeForFont(String(target.ssid)).c_str(), &inter_28, THEME_TEXT_MUTED, 244);
    if (name) { lv_obj_set_height(name, 80); lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL); }
    setupConnectingDrawn = true;
    setupConnectingAttempt = target.setupAttempt;
    setupConnectingStart = millis();
    setupConnectFailed = false;
    lv_refr_now(NULL);
}

// =============================================================================
// BLOCK FOUND DETECTION
// =============================================================================

void checkBlockFound() {
    int currentBlocks = (int)valids;

    // First call after boot: `valids` was just restored from NVS, so seed the
    // baseline to it. Without this, a device that has ever found a block (valids
    // > 0) would replay the full BLOCK FOUND celebration on every single boot,
    // because lastBlocksFound starts at 0. Only genuine NEW blocks should fire.
    static bool blockBaselineSeeded = false;
    if (!blockBaselineSeeded) {
        blockBaselineSeeded = true;
        lastBlocksFound = currentBlocks;
        return;
    }

    if (currentBlocks > lastBlocksFound && lastBlocksFound >= 0) {
        celebrationClockArmed=false;
        clearPage(); // Cancel a demo's scene/heading timers before the real find.
        navigationClickPending.store(false); // A prior-screen button must not dismiss a new find.
        bootLongPressFlag=false;
        // Rebuilding the page releases the previous scene and its timer.
        g_landingImg = nullptr;
        simCountdownActive = false;
        simCelebrationActive = false;
        simCountdownLabel = nullptr;
        showingBlockFoundScreen = false;
        houstonBuilt = false;
        clearDemoControls();
        audioStopBlockFound();
        celebrationHasPlayback=true;
        celebrationTour={};
        celebrationRecord=block_record::snapshot();
        // No countdown on a real find, so kick the audio hardware init off now —
        // it warms up concurrently while the landing frame builds and paints.
        audioPrewarm();
        showingLandingSeq = true;      // play the landing pre-roll, then Houston
        landingBuilt = false;
        celebrationPage = 0;    // every celebration starts on the artwork
        certBuilt = false;
        blockFoundAnimStart = millis();
        // Music starts in renderLandingSequence(), right after the first landing
        // frame is on the panel — see the note there.

        currentScreenIndex = NAV_MINING;  // Snap to mining screen so flash is visible
        wakeDisplay();
        triggerImmediateRedraw();
    }

    lastBlocksFound = currentBlocks;


}

// Strip non-ASCII bytes (e.g. iPhone's curly apostrophe U+2019) so only
// printable ASCII 0x20–0x7E is passed to the font renderer.
static String sanitizeForFont(const String &raw) {
    String out;
    size_t n = raw.length();
    out.reserve(n);
    for (size_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)raw.charAt(i);
        if (c >= 0x20 && c <= 0x7E) { out += (char)c; continue; }
        // The display font is ASCII-only, so any non-ASCII byte would otherwise be
        // dropped. Translate the common "smart" punctuation (UTF-8 E2 80 xx) to its
        // ASCII equivalent so names like "Alex's iPhone" — which use a curly
        // apostrophe (U+2019), not a plain ' — still render correctly.
        if (c == 0xE2 && (i + 2) < n && (uint8_t)raw.charAt(i + 1) == 0x80) {
            uint8_t t = (uint8_t)raw.charAt(i + 2);
            if      (t == 0x98 || t == 0x99) out += '\'';  // ' '  curly single quotes
            else if (t == 0x9C || t == 0x9D) out += '"';   // " "  curly double quotes
            else if (t == 0x93 || t == 0x94) out += '-';   // - -  en/em dashes
            i += 2;  // consume the rest of the 3-byte sequence
            continue;
        }
        // Other non-ASCII (accents, emoji, …): no glyph in the font — skip.
    }
    return out;
}

// Shared list scaffold — used by list-style screens
static void buildListScreen(const char *screenTitle, int navIndex,
                            const lv_font_t *titleFont = &inter_36_bold,
                            int titleTop = SAFE_TOP + 3) {
    if (!lv_scr_act()) return;
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), THEME_BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, screenTitle);
    lv_obj_set_style_text_font(title, titleFont, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT_WHITE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, titleTop);

    createPageDots(navIndex);
}

static lv_obj_t *buildList(void) {
    lv_obj_t *list = lv_obj_create(lv_scr_act());
    if (!list) { SERIAL_PRINTLN("[LVGL] buildList: NULL - out of memory?"); return NULL; }
    lv_obj_set_size(list, WIDTH - 20, HEIGHT - SAFE_BOTTOM - 100);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    return list;
}

static void addRow(lv_obj_t *list, const char *label, const char *value, lv_color_t valueColor) {
    if (!list) return;
    lv_obj_t *row = lv_obj_create(list);
    if (!row) return;
    lv_obj_set_size(row, LV_PCT(100), 58);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &inter_24, 0);
    lv_obj_set_style_text_color(lbl, THEME_TEXT_MUTED, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, &inter_28, 0);
    lv_obj_set_style_text_color(val, valueColor, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
}

// =============================================================================
// SCREEN 2: DEVICE - Hardware and config info
// =============================================================================

// --- Device screen retained LVGL objects ---
static struct {
    lv_obj_t *title;
    lv_obj_t *list;
    // Row value labels (updated each frame without rebuilding)
    lv_obj_t *wifiVal, *poolVal, *walletVal, *fwVal, *battVal;
} deviceUI = {};

static void buildDeviceScreen(void) {
    lv_obj_clean(lv_scr_act());
    // lv_obj_clean just destroyed every object these pointers referenced. Null
    // them immediately so a failed build below can't leave the update path
    // writing through dangling pointers (use-after-free). They're only valid
    // again once lastBuiltScreen == NAV_DEVICE at the end of a full build.
    deviceUI = {};
    lv_obj_set_style_bg_color(lv_scr_act(), THEME_BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    // Title
    deviceUI.title = lv_label_create(lv_scr_act());
    lv_label_set_text(deviceUI.title, "DEVICE");
    lv_obj_set_style_text_font(deviceUI.title, &inter_36_bold, 0);
    lv_obj_set_style_text_color(deviceUI.title, THEME_TEXT_WHITE, 0);
    lv_obj_align(deviceUI.title, LV_ALIGN_TOP_MID, 0, SAFE_TOP + 3);

    // List container
    deviceUI.list = lv_obj_create(lv_scr_act());
    if (!deviceUI.list) return;
    lv_obj_set_size(deviceUI.list, WIDTH - 20, HEIGHT - SAFE_BOTTOM - 100);
    lv_obj_align(deviceUI.list, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_flex_flow(deviceUI.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(deviceUI.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(deviceUI.list, 0, 0);
    lv_obj_set_style_pad_all(deviceUI.list, 0, 0);
    lv_obj_set_style_pad_row(deviceUI.list, 0, 0);
    lv_obj_clear_flag(deviceUI.list, LV_OBJ_FLAG_SCROLLABLE);

    // Build rows — we store the value label pointers for updates
    // WiFi
    addRow(deviceUI.list, "Wi-Fi", "", THEME_TEXT_MUTED);
    deviceUI.wifiVal = lv_obj_get_child(lv_obj_get_child(deviceUI.list, 0), 1);

    // Pool
    addRow(deviceUI.list, "Pool", "", THEME_TEXT_MUTED);
    deviceUI.poolVal = lv_obj_get_child(lv_obj_get_child(deviceUI.list, 1), 1);

    // Wallet
    addRow(deviceUI.list, "Wallet", "", THEME_TEXT_WHITE);
    deviceUI.walletVal = lv_obj_get_child(lv_obj_get_child(deviceUI.list, 2), 1);

    // Battery (live status) above Firmware (static reference info), per request.
    addRow(deviceUI.list, "Battery", "", THEME_TEXT_WHITE);
    deviceUI.battVal = lv_obj_get_child(lv_obj_get_child(deviceUI.list, 3), 1);
    // Firmware
    addRow(deviceUI.list, "Firmware", CURRENT_VERSION, THEME_TEXT_WHITE);
    deviceUI.fwVal = lv_obj_get_child(lv_obj_get_child(deviceUI.list, 4), 1);


    // Only declare the screen "built" if every value label the update path will
    // write to actually exists. If any row/child alloc failed under memory
    // pressure, leave lastBuiltScreen unset so the caller skips the update (and
    // retries the build next frame) instead of dereferencing a NULL child.
    if (!deviceUI.wifiVal || !deviceUI.poolVal || !deviceUI.walletVal ||
        !deviceUI.battVal || !deviceUI.fwVal) {
        SERIAL_PRINTLN("[LVGL] device screen build incomplete (OOM?) — will retry");
        return;
    }

    fitSingleLine(deviceUI.wifiVal, 250);
    fitSingleLine(deviceUI.poolVal, 250);

    createPageDots(NAV_DEVICE);
    lastBuiltScreen = NAV_DEVICE;
}

void waveshareDisplay_DeviceScreen(unsigned long mElapsed) {
    splash_cleanup();
    updateSmartDimming();
    if (displayOff || displayManuallyOff) return;
    if (!lv_scr_act()) return;

    // Build objects on first call or after screen switch
    if (lastBuiltScreen != NAV_DEVICE) {
        buildDeviceScreen();
        // Build failed (OOM) — deviceUI pointers are NULL. Skip the update this
        // frame rather than dereferencing them; it retries next frame.
        if (lastBuiltScreen != NAV_DEVICE) return;
    }

    // --- Update dynamic values ---
    int pct = getBatteryPercent();
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    bool poolOk = wifiOk && g_poolPhase.load() == POOL_PHASE_MINING;

    String networkName = WiFi.SSID();
    if (!networkName.length()) networkName = wifiTargetSnapshot().ssid;
    String ssid = sanitizeForFont(networkName);
    String pool = sanitizeForFont(String(Settings.PoolAddress));
    lv_label_set_text(deviceUI.wifiVal, ssid.length() ? ssid.c_str() : "Not set");
    lv_obj_set_style_text_color(deviceUI.wifiVal, wifiOk ? THEME_GREEN : lv_color_hex(0xF97316), 0);
    lv_label_set_text(deviceUI.poolVal, pool.length() ? pool.c_str() : "Not set");
    lv_obj_set_style_text_color(deviceUI.poolVal, poolOk ? THEME_GREEN : lv_color_hex(0xF97316), 0);

    // Wallet
    String wallet = String(Settings.BtcWallet);
    String walletShort;
    if (wallet.length() > 12) {
        walletShort = wallet.substring(0, 6) + "..." + wallet.substring(wallet.length() - 4);
    } else if (wallet.length() > 0) {
        walletShort = wallet;
    } else {
        walletShort = "Not set";
    }
    lv_label_set_text(deviceUI.walletVal, walletShort.c_str());

    // Battery percentage only, including while charging or fully charged.
    char battBuf[12];
    if (pct >= 0) {
        snprintf(battBuf, sizeof(battBuf), "%d%%", pct);
    } else {
        strcpy(battBuf, "--");
    }
    lv_label_set_text(deviceUI.battVal, battBuf);
    lv_obj_set_style_text_color(deviceUI.battVal, getBatteryColor(pct), 0);

}

// =============================================================================
// =============================================================================
// SETTINGS SCREEN
// =============================================================================

// Row geometry and touch bounds live together in ui/page_controls.h.

// Refined Settings cards retain the same large touch bounds and gesture handling.
static bool buildSettingsRow(const ui::PageRect& bounds, const char *name,
                             const char *valueText = nullptr,
                             lv_color_t valueColor = THEME_TEXT_MUTED) {
    auto* row = lv_obj_create(lv_scr_act());
    if (!row) return false;
    registerTouchTarget(row, bounds);
    lv_obj_set_size(row, bounds.width, bounds.height);
    lv_obj_set_pos(row, bounds.x, bounds.y);
    // Keep the charcoal neutral when quantized to the display's RGB565 colors.
    lv_obj_set_style_bg_color(row, lv_color_hex(0x151619), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 24, 0);
    lv_obj_set_style_pad_hor(row, 18, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    auto* label = lv_label_create(row);
    if (!label) return false;
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &inter_28_semi_bold, 0);
    lv_obj_set_style_text_color(label, THEME_TEXT_WHITE, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    auto* icon = lv_label_create(row);
    if (!icon) return false;
    lv_label_set_text(icon, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x848994), 0);
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, 0, 0);
    if (!valueText || !valueText[0]) return true;
    auto* value = lv_label_create(row);
    if (!value) return false;
    lv_label_set_text(value, valueText);
    lv_obj_set_style_text_font(value, &inter_24, 0);
    lv_obj_set_style_text_color(value, valueColor, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -32, 0);
    return true;
}

static bool buildBrightnessDetail() {
    clearPage();
    bool built = buildDetailHeader("Brightness");
    const int preset = settings::brightnessPreset(Settings.screenBrightness);
    const ui::PageRect bounds[] = {ui::brightnessLow, ui::brightnessMedium, ui::brightnessHigh};
    const int values[] = {60, 150, 230};
    for (int i=0; i<3; ++i) {
        auto* row = pageAction(bounds[i], settings::brightnessLabel(values[i]));
        if (!row) { built=false; continue; }
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_align(lv_obj_get_child(row,0), LV_ALIGN_LEFT_MID, 20, 0);
        if (preset == values[i]) {
            lv_obj_set_style_border_width(row, 2, 0);
            lv_obj_set_style_border_color(row, THEME_ORANGE, 0);
            auto* check = lv_label_create(row);
            if (!check) { built=false; continue; }
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_set_style_text_font(check, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(check, THEME_ORANGE, 0);
            lv_obj_align(check, LV_ALIGN_RIGHT_MID, -20, 0);
        }
    }
    return built;
}

static bool buildBatterySaverDetail() {
    clearPage();
    const bool header = buildDetailHeader("Battery Saver");
    auto* row = pageAction(ui::saverToggle, Settings.batterySaver ? "On" : "Off");
    if (row) {
        lv_obj_set_style_radius(row,18,0);
        auto* label = lv_obj_get_child(row,0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_set_style_text_color(label, Settings.batterySaver ? THEME_GREEN : THEME_TEXT_MUTED, 0);
        auto* toggle = lv_obj_create(row);
        if (toggle) {
            lv_obj_remove_style_all(toggle);
            lv_obj_set_size(toggle, 64, 36);
            lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -18, 0);
            lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(toggle, Settings.batterySaver ? THEME_GREEN : lv_color_hex(0x45474D), 0);
            lv_obj_clear_flag(toggle, LV_OBJ_FLAG_SCROLLABLE);
            auto* knob = lv_obj_create(toggle);
            if (knob) {
                lv_obj_remove_style_all(knob);
                lv_obj_set_size(knob, 28, 28);
                lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(knob, THEME_TEXT_WHITE, 0);
                lv_obj_align(knob, Settings.batterySaver ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                             Settings.batterySaver ? -4 : 4, 0);
            }
        }
    }
    auto* detail = pageText(Settings.batterySaver
        ? "Mining and Wi-Fi pause after\n3 minutes of inactivity."
        : "Mining continues while\nconnected. Uses more battery.", &inter_24, THEME_TEXT_WHITE, 245);
    auto* wake = pageText(Settings.batterySaver
        ? "Touch or press a button\nto resume."
        : "The screen can sleep while\nmining continues.", &inter_24, THEME_TEXT_MUTED, 337);
    auto* usb = Settings.batterySaver ? pageText("Mining stays active while\nplugged in.", &inter_20, THEME_TEXT_MUTED, 421) : nullptr;
    for (auto* label : {detail, wake, usb}) {
        if (label) { lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0); lv_obj_set_style_text_line_space(label, 3, 0); }
    }
    return header && row && detail && wake && (!Settings.batterySaver || usb);
}

static bool buildSetupConfirmation() {
    clearPage();
    const bool header = buildDetailHeader("Phone setup");
    auto* detail = pageText("Change Wi-Fi, wallet, pool\nand time from your phone.", &inter_24, THEME_TEXT_WHITE, 110);
    auto* notice = pageText("Your settings are kept.\nMining pauses during setup.", &inter_24, THEME_TEXT_MUTED, 198);
    for(auto* label : {detail,notice}) if(label) {
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_LEFT,0);
        lv_obj_set_style_text_line_space(label,5,0);
    }
    auto* open = pageAction(ui::openSetup, "Open setup", true);
    auto* cancel = pageAction(ui::cancelSetup, "Cancel");
    for (auto* button : {open, cancel})
        if (button) lv_obj_set_style_radius(button, 18, 0);
    return header && detail && notice && open && cancel;
}

void waveshareDisplay_SettingsScreen(unsigned long mElapsed) {
    splash_cleanup();
    updateSmartDimming();
    if (displayOff || displayManuallyOff || !lv_scr_act()) return;
    const auto action = static_cast<ui::PageAction>(settingsTappedAction);
    settingsTappedAction = static_cast<int>(ui::PageAction::None);
    bool redraw = lastBuiltScreen != NAV_SETTINGS;
    if (action == ui::PageAction::Brightness && settingsView == ui::SettingsView::List) {
        settingsView = ui::SettingsView::Brightness;
        redraw = true;
    } else if (settingsView == ui::SettingsView::Brightness && ui::selectedBrightness(action) >= 0) {
        const int brightness = ui::selectedBrightness(action);
        if (Settings.screenBrightness != brightness) {
            Settings.screenBrightness = brightness;
            setBrightness(Settings.screenBrightness);
            markSettingsDirty();
            redraw = true;
        }
    } else if (action == ui::PageAction::BatterySaver && settingsView == ui::SettingsView::List) {
        settingsView = ui::SettingsView::BatterySaver;
        redraw = true;
    } else if (action == ui::PageAction::ToggleSaver && settingsView == ui::SettingsView::BatterySaver) {
        Settings.batterySaver = !Settings.batterySaver;
        device::requestBatterySaver(Settings.batterySaver);
        markSettingsDirty();
        redraw = true;
    } else if (action == ui::PageAction::ShowSetup && settingsView == ui::SettingsView::List) {
        settingsView = ui::SettingsView::SetupConfirmation;
        redraw = true;
    } else if (action == ui::PageAction::Back && settingsView != ui::SettingsView::List) {
        const bool returnToMining = settingsView == ui::SettingsView::SetupConfirmation && setupReturnScreen == NAV_MINING;
        settingsView = ui::SettingsView::List;
        if (returnToMining) {
            currentScreenIndex = NAV_MINING; invalidateScreen(); triggerImmediateRedraw(); return;
        }
        redraw = true;
    } else if (action == ui::PageAction::OpenSetup && settingsView == ui::SettingsView::SetupConfirmation) {
        flushPendingSettings(true);
        reboot_to_setup();
        return;
    }
    if (!redraw) return;
    lastBuiltScreen = -1;
    if (settingsView == ui::SettingsView::SetupConfirmation) {
        if (buildSetupConfirmation()) lastBuiltScreen = NAV_SETTINGS;
        return;
    }
    if (settingsView == ui::SettingsView::BatterySaver) {
        if (buildBatterySaverDetail()) lastBuiltScreen = NAV_SETTINGS;
        return;
    }
    if (settingsView == ui::SettingsView::Brightness) {
        if (buildBrightnessDetail()) lastBuiltScreen = NAV_SETTINGS;
        return;
    }
    buildListScreen("SETTINGS", NAV_SETTINGS, &inter_28_semi_bold, SAFE_TOP + 8);
    const int preset = settings::brightnessPreset(Settings.screenBrightness);
    const bool brightnessRow = buildSettingsRow(ui::brightness, "Brightness",
        settings::brightnessLabel(preset), lv_color_hex(0xB7BAC3));
    const bool saverRow = buildSettingsRow(ui::batterySaver, "Battery Saver",
        Settings.batterySaver ? "On" : "Off",
        Settings.batterySaver ? THEME_GREEN : lv_color_hex(0xB7BAC3));
    const bool setupRow = buildSettingsRow(ui::setup, "Wi-Fi & setup");
    if (brightnessRow && saverRow && setupRow) lastBuiltScreen = NAV_SETTINGS;
}

// =============================================================================
// SCREEN 0: HOME (Mining) - 3-column footer
// =============================================================================

// --- Mining screen retained LVGL objects ---
static struct {
    lv_obj_t *time_lbl;
    lv_obj_t *battOutline, *battTip, *battFill, *battLabel;
    lv_obj_t *hr_lbl, *unit_lbl;
    lv_obj_t *attempts_val, *uptime_val, *blocks_val;
} miningUI = {};

static void buildMiningScreen(void) {
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), THEME_BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    // Status bar — time
    miningUI.time_lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(miningUI.time_lbl, &inter_36, 0);
    lv_obj_set_pos(miningUI.time_lbl, SAFE_LEFT + 40, SAFE_TOP + 5);

    // Battery indicator (retained: outline, tip, fill, label)
    int bx = WIDTH - SAFE_RIGHT - 100, by = SAFE_TOP + 12;
    miningUI.battOutline = lv_obj_create(lv_scr_act());
    lv_obj_set_size(miningUI.battOutline, 70, 32);
    lv_obj_set_pos(miningUI.battOutline, bx, by);
    lv_obj_set_style_bg_opa(miningUI.battOutline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(miningUI.battOutline, BATTERY_OUTLINE_COLOR, 0);
    lv_obj_set_style_border_width(miningUI.battOutline, 3, 0);
    lv_obj_set_style_radius(miningUI.battOutline, 8, 0);

    miningUI.battTip = lv_obj_create(lv_scr_act());
    lv_obj_set_size(miningUI.battTip, 6, 16);
    lv_obj_set_pos(miningUI.battTip, bx + 70, by + 8);
    lv_obj_set_style_bg_color(miningUI.battTip, BATTERY_OUTLINE_COLOR, 0);
    lv_obj_set_style_border_width(miningUI.battTip, 0, 0);
    lv_obj_set_style_radius(miningUI.battTip, 3, 0);

    miningUI.battFill = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(miningUI.battFill, bx + 5, by + 5);
    lv_obj_set_style_bg_opa(miningUI.battFill, LV_OPA_70, 0);
    lv_obj_set_style_border_width(miningUI.battFill, 0, 0);
    lv_obj_set_style_radius(miningUI.battFill, 4, 0);

    miningUI.battLabel = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(miningUI.battLabel, &inter_24, 0);
    lv_obj_set_style_text_color(miningUI.battLabel, THEME_TEXT_WHITE, 0);

    // Hashrate (shown when no block found)
    miningUI.hr_lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(miningUI.hr_lbl, &inter_108_semi_bold, 0);
    lv_obj_set_style_text_color(miningUI.hr_lbl, THEME_TEXT_WHITE, 0);
    lv_obj_align(miningUI.hr_lbl, LV_ALIGN_CENTER, 0, -55);

    miningUI.unit_lbl = lv_label_create(lv_scr_act());
    lv_label_set_text(miningUI.unit_lbl, "KH/s");
    lv_obj_set_style_text_font(miningUI.unit_lbl, &inter_48, 0);
    lv_obj_set_style_text_color(miningUI.unit_lbl, THEME_ORANGE, 0);
    lv_obj_align(miningUI.unit_lbl, LV_ALIGN_CENTER, 0, 35);


    // Footer divider
    int footerY = HEIGHT - SAFE_BOTTOM - 95;
    int colW = WIDTH / 3;

    lv_obj_t *div = lv_obj_create(lv_scr_act());
    lv_obj_set_size(div, 120, 4);
    lv_obj_set_style_bg_color(div, THEME_TEXT_DARK, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_50, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, footerY - 15);

    // Hashes (lifetime hash count)
    miningUI.attempts_val = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(miningUI.attempts_val, &inter_36, 0);
    lv_obj_set_style_text_color(miningUI.attempts_val, THEME_TEXT_WHITE, 0);
    lv_obj_align(miningUI.attempts_val, LV_ALIGN_TOP_MID, -(colW - 5), footerY);
    lv_obj_t *hl = lv_label_create(lv_scr_act());
    lv_label_set_text(hl, "Hashes");
    lv_obj_set_style_text_font(hl, &inter_28, 0);
    lv_obj_set_style_text_color(hl, THEME_TEXT_MUTED, 0);
    lv_obj_align(hl, LV_ALIGN_TOP_MID, -(colW - 5), footerY + 40);

    // Uptime
    miningUI.uptime_val = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(miningUI.uptime_val, &inter_36, 0);
    lv_obj_set_style_text_color(miningUI.uptime_val, THEME_TEXT_WHITE, 0);
    lv_obj_align(miningUI.uptime_val, LV_ALIGN_TOP_MID, 4, footerY);
    lv_obj_t *ul = lv_label_create(lv_scr_act());
    lv_label_set_text(ul, "Uptime");
    lv_obj_set_style_text_font(ul, &inter_28, 0);
    lv_obj_set_style_text_color(ul, THEME_TEXT_MUTED, 0);
    lv_obj_align(ul, LV_ALIGN_TOP_MID, 4, footerY + 40);

    // Blocks
    miningUI.blocks_val = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(miningUI.blocks_val, &inter_36, 0);
    lv_obj_align(miningUI.blocks_val, LV_ALIGN_TOP_MID, colW + 3, footerY);
    lv_obj_t *bl = lv_label_create(lv_scr_act());
    lv_label_set_text(bl, "Blocks");
    lv_obj_set_style_text_font(bl, &inter_28, 0);
    lv_obj_set_style_text_color(bl, THEME_TEXT_MUTED, 0);
    lv_obj_align(bl, LV_ALIGN_TOP_MID, colW + 3, footerY + 40);

    if(block_record::snapshot().valid()) {
        lv_obj_set_style_text_color(bl,THEME_ORANGE,0);
        lv_obj_set_style_text_decor(bl,LV_TEXT_DECOR_UNDERLINE,0);
        auto* hit=lv_obj_create(lv_scr_act());
        if(hit) {
            lv_obj_remove_style_all(hit);lv_obj_set_pos(hit,ui::miningBlocks.x,ui::miningBlocks.y);
            lv_obj_set_size(hit,ui::miningBlocks.width,ui::miningBlocks.height);
            lv_obj_set_style_bg_opa(hit,LV_OPA_TRANSP,0);
            lv_obj_set_style_bg_opa(hit,LV_OPA_20,LV_STATE_PRESSED);
            lv_obj_set_style_radius(hit,12,0);registerTouchTarget(hit,ui::miningBlocks);
        }
    }
    createPageDots(NAV_MINING);
    lastBuiltScreen = NAV_MINING;
}

void waveshareDisplay_HomeScreen(unsigned long mElapsed) {
    splash_cleanup();
    updateSmartDimming();
    if (displayOff || displayManuallyOff) return;
    if (!lv_scr_act()) return;

    // Build objects on first call or after screen switch
    if (lastBuiltScreen != NAV_MINING) {
        buildMiningScreen();
    }

    // --- Update dynamic values only ---
    mining_data data = getMiningData(mElapsed);
    float hr = data.currentHashRate.toFloat();

    // Time
    lv_label_set_text(miningUI.time_lbl, initialTime > 0 ? formatTimeClean(getTime()).c_str() : "--:--");
    lv_obj_set_style_text_color(miningUI.time_lbl, initialTime > 0 ? THEME_TEXT_WHITE : THEME_TEXT_MUTED, 0);

    // Battery
    int percent = getBatteryPercent();
    bool charging = isCharging();
    lv_color_t battColor = getBatteryColor(percent);
    int bx = WIDTH - SAFE_RIGHT - 100;
    if (percent > 0) {
        int fillWidth = (percent * 60) / 100;
        if (fillWidth < 6) fillWidth = 6;
        lv_obj_set_size(miningUI.battFill, fillWidth, 22);
        lv_obj_set_style_bg_color(miningUI.battFill, charging ? THEME_GREEN : battColor, 0);
        lv_obj_clear_flag(miningUI.battFill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(miningUI.battFill, LV_OBJ_FLAG_HIDDEN);
    }
    char battText[8];
    snprintf(battText, sizeof(battText), percent >= 0 ? "%d" : "--", percent);
    lv_label_set_text(miningUI.battLabel, battText);
    lv_obj_update_layout(miningUI.battLabel);
    lv_obj_align_to(miningUI.battLabel, miningUI.battOutline, LV_ALIGN_CENTER, 0, 0);

    const bool usable = !device::batterySaverIdle() && hr > 0 &&
        ui::showHashrate(network::wifiStage() == network::WifiStage::Connected,
                         g_poolPhase.load(), g_thermalTier.load() >= 3);
    // Keep the same large readout in every state; the clock owns guidance.
    char hrBuf[16];
    const char* unit = "KH/s";
    if (!usable) snprintf(hrBuf, sizeof(hrBuf), "---");
    else if (hr >= 1000) { snprintf(hrBuf, sizeof(hrBuf), "%.2f", hr / 1000.0); unit = "MH/s"; }
    else snprintf(hrBuf, sizeof(hrBuf), "%.1f", hr);
    lv_label_set_text(miningUI.hr_lbl, hrBuf);
    lv_label_set_text(miningUI.unit_lbl, unit);

    // Footer values
    char hashBuf[20];
    unsigned long kh = strtoul(data.totalKHashes.c_str(), NULL, 10);
    if (kh >= 1000000) snprintf(hashBuf, sizeof(hashBuf), "%luB", kh / 1000000);
    else if (kh >= 1000) snprintf(hashBuf, sizeof(hashBuf), "%luM", kh / 1000);
    else if (kh > 0) snprintf(hashBuf, sizeof(hashBuf), "%luK", kh);
    else strcpy(hashBuf, "0");
    lv_label_set_text(miningUI.attempts_val, hashBuf);

    lv_label_set_text(miningUI.uptime_val, formatUptime(data.timeMining.c_str()));

    char blkBuf[8];
    int blocksCount = data.valids.toInt();
    snprintf(blkBuf, sizeof(blkBuf), "%d", blocksCount);
    lv_label_set_text(miningUI.blocks_val, blkBuf);
    lv_obj_set_style_text_color(miningUI.blocks_val, (blocksCount > 0) ? THEME_ORANGE : THEME_TEXT_WHITE, 0);
}

// =============================================================================
// SCREEN 1: CLOCK - Default screen with connection status
// =============================================================================

// --- Clock screen retained LVGL objects ---
static struct {
    lv_obj_t *date_lbl, *time_lbl, *timeHint;
    lv_obj_t *statusContainer;     // fixed primary row + optional subtitle
    lv_obj_t *statusDot;
    lv_obj_t *statusRow;           // centered dot + main status, independent of subtitle
    lv_obj_t *statusLine1, *statusLine2;
    bool dotVisible;
    bool miningActive;
    uint32_t miningStartedAt;
} clockUI = {};

// Reuse the normal clock refresh; no animation timer or extra wakeups.
static void updateClockActivity(bool mining, unsigned long now) {
    // Start each Mining status with one full second of green, independent of
    // system uptime. Unsigned subtraction remains safe when millis() wraps.
    if (mining && !clockUI.miningActive) clockUI.miningStartedAt = now;
    clockUI.miningActive = mining;
    const uint32_t elapsed = static_cast<uint32_t>(now - clockUI.miningStartedAt);
    const bool visible = !mining || (elapsed / 1000UL) % 2UL == 0;
    if (visible == clockUI.dotVisible) return;
    clockUI.dotVisible = visible;
    lv_obj_set_style_opa(clockUI.statusDot, visible ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void buildClockScreen(void) {
    lv_obj_clean(lv_scr_act());
    clockUI.dotVisible = true;
    clockUI.miningActive = false;
    lv_obj_set_style_bg_color(lv_scr_act(), THEME_BG_PRIMARY, 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    // Quiet star accents on this screen only; no animated background work.
    clock_stars::create(lv_scr_act());

    // Date
    clockUI.date_lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(clockUI.date_lbl, &inter_48, 0);
    lv_obj_set_style_text_color(clockUI.date_lbl, THEME_ORANGE, 0);
    lv_obj_align(clockUI.date_lbl, LV_ALIGN_TOP_RIGHT, -SAFE_RIGHT - 10, SAFE_TOP + 40);

    // Time
    clockUI.time_lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(clockUI.time_lbl, &inter_96, 0);

    clockUI.timeHint = pageText("Waiting for time", &inter_24, THEME_TEXT_MUTED, 265);
    if (clockUI.timeHint) lv_obj_add_flag(clockUI.timeHint, LV_OBJ_FLAG_HIDDEN);

    // Fixed layout: optional guidance must not move the main label or its dot.
    clockUI.statusContainer = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(clockUI.statusContainer);
    lv_obj_set_size(clockUI.statusContainer, ui::clockStatus.width, ui::clockStatus.height);
    lv_obj_set_pos(clockUI.statusContainer, ui::clockStatus.x, ui::clockStatus.y);
    lv_obj_clear_flag(clockUI.statusContainer, LV_OBJ_FLAG_SCROLLABLE);
    clockUI.statusRow = lv_obj_create(clockUI.statusContainer);
    lv_obj_remove_style_all(clockUI.statusRow);
    lv_obj_set_size(clockUI.statusRow, ui::clockStatus.width, inter_36.line_height);
    lv_obj_set_pos(clockUI.statusRow, 0, 20);
    lv_obj_clear_flag(clockUI.statusRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(clockUI.statusRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clockUI.statusRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(clockUI.statusRow, 12, 0);

    // Status dot
    clockUI.statusDot = lv_obj_create(clockUI.statusRow);
    lv_obj_remove_style_all(clockUI.statusDot);
    lv_obj_clear_flag(clockUI.statusDot, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_opa(clockUI.statusDot, LV_OPA_COVER, 0);
    lv_obj_set_size(clockUI.statusDot, 18, 18);
    lv_obj_set_style_radius(clockUI.statusDot, 9, 0);

    // Status line 1 (always present)
    clockUI.statusLine1 = lv_label_create(clockUI.statusRow);
    lv_obj_set_style_text_font(clockUI.statusLine1, &inter_36, 0);
    lv_obj_set_style_text_color(clockUI.statusLine1, THEME_TEXT_WHITE, 0);

    clockUI.statusLine2 = lv_label_create(clockUI.statusContainer);
    lv_obj_set_style_text_font(clockUI.statusLine2, &inter_24, 0);
    lv_obj_set_style_text_color(clockUI.statusLine2, THEME_TEXT_MUTED, 0);
    lv_label_set_long_mode(clockUI.statusLine2, LV_LABEL_LONG_DOT);
    lv_obj_set_size(clockUI.statusLine2, 330, inter_24.line_height);
    lv_obj_set_style_text_align(clockUI.statusLine2, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(clockUI.statusLine2, (ui::clockStatus.width - 330) / 2,
                   20 + inter_36.line_height + 2);
    lv_obj_add_flag(clockUI.statusLine2, LV_OBJ_FLAG_HIDDEN);

    if(block_record::snapshot().unread) {
        auto* notice=pageAction(ui::blockNotice,"Block found");
        if(notice) {
            auto* label=lv_obj_get_child(notice,0);
            lv_obj_set_style_text_font(label,&inter_24,0);
            lv_obj_set_style_text_color(label,THEME_ORANGE,0);
            lv_obj_center(label);
        }
    }
    createPageDots(NAV_CLOCK);
    lastBuiltScreen = NAV_CLOCK;
}

static void updateClockStatus(const ui::MiningStatus& status, const char* networkName,
                              unsigned long now) {
    lv_label_set_text(clockUI.statusLine1, status.clock);
    // Normal Wi-Fi progress names the saved network. Only a sustained problem
    // replaces it with guidance; steady mining stays a single, calm line.
    const char* subtitle = status.detail;
    if (!subtitle[0] && strcmp(status.clock, "Connecting to Wi-Fi") == 0)
        subtitle = networkName;
    if (subtitle && subtitle[0]) {
        // Resolve the main row's flex position before aligning its subtitle.
        // The dot/primary row keeps its existing centered position.
        lv_obj_update_layout(clockUI.statusRow);
        const int textX=lv_obj_get_x(clockUI.statusLine1);
        lv_obj_set_x(clockUI.statusLine2,textX);
        lv_obj_set_width(clockUI.statusLine2,ui::clockStatus.width-textX);
        lv_label_set_text(clockUI.statusLine2, subtitle);
        lv_obj_clear_flag(clockUI.statusLine2, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(clockUI.statusLine2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(clockUI.statusDot, statusColor(status.tone), 0);
    updateClockActivity(status.tone == ui::StatusTone::Good, now);
}

void waveshareDisplay_ClockScreen(unsigned long mElapsed) {
    splash_cleanup();
    updateSmartDimming();
    checkRTCSync();
    if (displayOff || displayManuallyOff) return;
    if (!lv_scr_act()) return;

    // Build objects on first call or after screen switch
    if (lastBuiltScreen != NAV_CLOCK) {
        buildClockScreen();
    }

    // --- Update dynamic values ---
    mining_data data = getMiningData(mElapsed);
    float hr = data.currentHashRate.toFloat();

    // Date
    bool timeValid = (initialTime > 0);
    lv_label_set_text(clockUI.date_lbl, timeValid ? getDateFromRTC().c_str() : "");

    // Time
    lv_label_set_text(clockUI.time_lbl, timeValid ? formatTimeClean(getTime()).c_str() : "--:--");
    if (clockUI.timeHint) {
        if (timeValid) lv_obj_add_flag(clockUI.timeHint, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(clockUI.timeHint, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_font(clockUI.time_lbl, &inter_96, 0);
    lv_obj_set_style_text_color(clockUI.time_lbl, timeValid ? THEME_TEXT_WHITE : THEME_TEXT_MUTED, 0);
    lv_obj_align_to(clockUI.time_lbl, clockUI.date_lbl, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 5);

    const auto wifiStage = network::wifiStage();
    const auto status = ui::connectionStatus(wifiStage == network::WifiStage::Connected, g_poolPhase.load(),
        g_thermalTier.load() >= 3, hr > 0, g_poolRetryCount.load(), device::batterySaverIdle(),
        wifiStage, network::wifiNeedsAttention(), network::wifiProblem());
    const String ssid = sanitizeForFont(String(wifiTargetSnapshot().ssid));
    updateClockStatus(status, ssid.c_str(), millis());
}


// =============================================================================
// ANIMATION
// =============================================================================

void waveshareDisplay_ScreenDispatcher(unsigned long frame);
static void handleNavigationButton();

void waveshareDisplay_AnimateScreen(unsigned long frame) {
    (void)frame;
    // Check for block found on every frame, regardless of which screen is active
    checkBlockFound();
    if(navigationClickPending.exchange(false))handleNavigationButton();

    // BOOT long-press (3s) triggers Block Found simulation only from the
    // main screens (clock, mining, device, settings) — not from charging,
    // WiFi config, setup wizard, etc.
    if (bootLongPressFlag) {
        bootLongPressFlag = false;
        bool onMainScreen = (currentScreenIndex >= NAV_CLOCK &&
                             currentScreenIndex <= NAV_SETTINGS);
        if (onMainScreen && !simCountdownActive && !simCelebrationActive && !showingLandingSeq && !showingBlockFoundScreen) {
            startBlockFoundSimulation();
        }
    }


    // Skip LVGL processing when display is off to save battery
    if (!displayOff && !displayManuallyOff) {
        // Drive the count-in and its handoff from every animation frame. It
        // must not wait for the once-per-second mining-data redraw to advance.
        if (simCountdownActive) waveshareDisplay_ScreenDispatcher(1000);
        // Don't wait for the once-per-second data refresh to end the splash.
        if (splashActive && uint32_t(millis() - splashStartMs) >= SPLASH_MIN_MS)
            waveshareDisplay_ScreenDispatcher(1000);
        if (panelRedrawRequested.exchange(false)) {
            waveshareDisplay_ScreenDispatcher(1000);
            lv_obj_invalidate(lv_scr_act());
            lv_refr_now(NULL); // Push refreshed labels before restoring brightness.
            if(revealAfterRedraw.exchange(false)) {
                bool held=disp_state_mutex!=NULL && xSemaphoreTake(disp_state_mutex,portMAX_DELAY)==pdTRUE;
                if(!displayOff && !displayManuallyOff && displayAwake)
                    setBrightness(Settings.screenBrightness>0 ? Settings.screenBrightness : DISPLAY_WAKE_BRIGHTNESS_DEFAULT);
                if(held)xSemaphoreGive(disp_state_mutex);
            }
        }
        lv_timer_handler();
    }
}

void waveshareDisplay_DoLedStuff(unsigned long frame) {}

// =============================================================================
// BUTTON HANDLING - Two-phase design
//
// Phase 1  checkWaveshareButtons()       — GPIO reads + PWR actions
//          Safe to call OUTSIDE the LVGL/display-bus mutex.
//          GPIO digitalRead() is always thread-safe.
//          PMU (pmu.shutdown) and esp_deep_sleep use separate I2C — no bus conflict.
//
// Phase 2  processWaveshareButtonActions() — display bus writes (setBrightness)
//          Must be called INSIDE the LVGL mutex so it doesn't race the display driver.
// =============================================================================

extern void reboot_to_setup(void);

#define PWR_OFF_HOLD_MS       3000  // Leave time to save before the hardware cutoff

static volatile bool pwrShortFlag   = false;
static volatile bool pwrLongFlag    = false;

static void showSetupShortcutPrompt(bool failed = false) {
    if (!lvgl_mutex || xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    static lv_obj_t* prompt = nullptr;
    if (prompt) { lv_obj_delete(prompt); prompt = nullptr; }
    auto* label = lv_label_create(lv_layer_top());
    if (label) {
        lv_label_set_text(label, failed ? "Couldn't open setup.\nPlease try again."
                                      : "Release both buttons\nto open setup");
        lv_obj_set_width(label, 350);
        lv_obj_set_style_text_font(label, &inter_24, 0);
        lv_obj_set_style_text_color(label, THEME_TEXT_WHITE, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(label, lv_color_hex(0x151619), 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(label, 18, 0);
        lv_obj_set_style_pad_all(label, 16, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        if (failed) lv_obj_delete_delayed(label, 2500);
        else prompt = label;
        lv_refr_now(NULL);
    }
    xSemaphoreGive(lvgl_mutex);
}

// PWR: an awake charging tap starts the watch; otherwise a tap toggles the display.
// A deliberate three-second hold powers off.
// Wake on press-down and consume its release. A continued hold still powers off.
// Ignore a button held during boot. Hold fires before the PMU's hard cutoff.
static void pollPwrButton() {
    static bool lastState = LOW;   // idle = LOW (external pull-down; pressed = HIGH)
    static uint32_t pressStart = 0;
    static bool bootSuppressed = false;  // ignore boot-hold entirely
    static bool longFired = false;       // power-off already triggered this press
    static bool shortFiredEarly = false; // wake already fired on press-down
    static bool initialized = false;

    bool state = digitalRead(PWR_BUTTON_PIN);
    if (setupButtonsConsumed.load()) {
        lastState = state;
        pressStart = millis();
        bootSuppressed = state == HIGH;
        initialized = longFired = shortFiredEarly = true;
        pwrShortFlag = pwrLongFlag = false;
        return;
    }

    // --- Detect new press (LOW → HIGH) ---
    if (lastState == LOW && state == HIGH) {
        pressStart = millis();
        longFired = false;
        shortFiredEarly = false;
        device::noteUserActivity();
        lastWakeTime = millis();  // Any button press resets dim/off timer
        SERIAL_PRINTLN("[BTN] PWR press start");

        // Instant wake: if display is fully off, fire immediately on
        // press-down — don't wait for release. Only for fully-off state;
        // a dimmed screen wakes on release.
        if (!bootSuppressed && (displayOff || displayManuallyOff)) {
            pwrShortFlag = true;
            shortFiredEarly = true;
            SERIAL_PRINTLN("[BTN] PWR instant wake (press-down)");
        }
    }

    // --- Deliberate power-off fires during hold, before the PMU cutoff ---
    // Fires regardless of whether the press woke the screen from off: a brief
    // wake tap never reaches three seconds, so a deliberate hold should always power off, even if it also happened to wake the display on press-down.
    // (Still skipped for a button-already-held-at-boot.)
    if (state == HIGH && !longFired && !bootSuppressed) {
        if (millis() - pressStart >= PWR_OFF_HOLD_MS) {
            longFired = true;
            pwrLongFlag = true;
            SERIAL_PRINTLN("[BTN] PWR hold — power-off requested");
        }
    }

    // --- Short press fires on release to sleep an awake display ---
    if (lastState == HIGH && state == LOW) {
        if (bootSuppressed) {
            bootSuppressed = false;
            SERIAL_PRINTLN("[BTN] PWR boot-hold release — ignored");
        } else if (!longFired && !shortFiredEarly) {
            uint32_t hold = millis() - pressStart;
            if (hold >= 20) {  // debounce: ignore sub-20ms glitches
                pwrShortFlag = true;
                SERIAL_PRINTF("[BTN] PWR short press (%lu ms)\n", hold);
            }
        }
        longFired = false;
        shortFiredEarly = false;
    }

    // --- First-call init: detect button already held at boot ---
    if (!initialized) {
        initialized = true;
        if (state == HIGH) {
            bootSuppressed = true;
            longFired = true;  // also block a held-at-boot power-off
            SERIAL_PRINTLN("[BTN] PWR already held at boot — suppressing");
        }
    }

    lastState = state;
}

// --- BOOT button: INPUT_PULLUP, active LOW ---
//   Short click → waveshareButtonClick() (switch screen / toggle setup)
//   Long hold (5s) → triggers Block Found simulation
//   Tap fires on release so a hold cannot also change the screen.
//   Cooldown prevents the same press from firing twice.
static void pollBootButton() {
    static bool lastState = HIGH;  // idle = HIGH (pullup, not pressed)
    static uint32_t pressStart = 0;
    static bool longPressFired = false;
    static uint32_t lastClickTime = 0; // Cooldown between clicks

    bool state = digitalRead(BOOT_BUTTON_PIN);  // LOW = pressed
    if (setupButtonsConsumed.load()) {
        lastState = state;
        pressStart = lastClickTime = millis();
        longPressFired = true;
        bootWakeConsumed = bootLongPressFlag = false;
        return;
    }

    // Falling edge: button just pressed. Only note the time — the tap action
    // fires on RELEASE (below), because firing it here would also trigger it on
    // the way into a long press (holding for the block-found simulation would
    // toggle the screen first).
    if (lastState == HIGH && state == LOW) {
        pressStart = millis();
        longPressFired = false;
        device::noteUserActivity();
        lastWakeTime = millis();  // Any button press resets dim/off timer
    }

    // Hold BOOT for five seconds to launch the demo.
    if (state == LOW && !longPressFired) {
        uint32_t held = millis() - pressStart;
        if (held >= 5000) {
            longPressFired = true;
            bool onMainScreen = (currentScreenIndex >= NAV_CLOCK &&
                                 currentScreenIndex <= NAV_SETTINGS);
            if (onMainScreen && !inSetupMode && !chargingBootMode) {
                bootLongPressFlag = true;
            }
        }
    }

    // Rising edge: released. Fire the tap action only if the hold didn't already
    // claim this press. Costs the length of the tap in latency (~50-150ms), which
    // is imperceptible, and it keeps tap and hold cleanly separate.
    // 150ms cooldown prevents bouncy/phantom double-fires.
    if (lastState == LOW && state == HIGH) {
        if (!longPressFired && !bootWakeConsumed && (millis() - lastClickTime >= 150)) {
            lastClickTime = millis();
            waveshareButtonClick();
        }
        bootWakeConsumed = false;
        longPressFired = false;
    }

    lastState = state;
}

// =============================================================================
// EXPORTED FUNCTIONS - Called from lv_tick_task (and previously loop)
// =============================================================================

// Poll GPIO + execute button actions — safe to call outside LVGL mutex.
// Called from lv_tick_task so buttons work even during WiFiManager portal.
void checkWaveshareButtons(void) {
    // Heartbeat: confirm this function is being called + show raw GPIO
    static unsigned long lastHB = 0;
    unsigned long now = millis();
    if (now - lastHB >= 2000) {
        lastHB = now;
        SERIAL_PRINTF("[BTN] heartbeat t=%lums  BOOT(%d)=%d  PWR(%d)=%d\n",
                      now,
                      BOOT_BUTTON_PIN, digitalRead(BOOT_BUTTON_PIN),
                      PWR_BUTTON_PIN,  digitalRead(PWR_BUTTON_PIN));
    }

    const auto shortcut = setupShortcut.update(digitalRead(BOOT_BUTTON_PIN) == LOW,
                                               digitalRead(PWR_BUTTON_PIN) == HIGH,
                                               millis());
    setupButtonsConsumed.store(shortcut.consume);
    if (shortcut.consume) {
        // Claim the gesture before either single-button hold can fire.
        navigationClickPending.store(false);
        pwrShortFlag = pwrLongFlag = bootLongPressFlag = false;
    }
    pollPwrButton();
    pollBootButton();
    if (shortcut.consume) {
        if (shortcut.started) {
            device::noteUserActivity();
            lastWakeTime = millis();
            wakeDisplay();
        }
        if (shortcut.armed) {
            audioStopBlockFound();
            showSetupShortcutPrompt();
        }
        if (shortcut.restart) {
            // BOOT must be released before restarting. Preserve recent Settings
            // edits under the UI lock; reboot_to_setup checkpoints statistics.
            if (lvgl_mutex && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                flushPendingSettings(true);
                xSemaphoreGive(lvgl_mutex);
            }
            reboot_to_setup();
            // A successful restart never returns. A storage failure must not
            // silently reboot into normal operation.
            showSetupShortcutPrompt(true);
        }
        return;
    }
    if(pwrShortFlag) {
        pwrShortFlag=false;
        if(displayOff || displayManuallyOff || !displayAwake)wakeDisplay();
        else if(chargingBootMode)chargingBootMode=false;
        else sleepDisplayManually();
    }
    if(pwrLongFlag) {
        pwrLongFlag=false;
        audioStopBlockFound();
        // Show the deliberate action, with enough margin before PMU hard-off.
        if(lvgl_mutex && xSemaphoreTake(lvgl_mutex,pdMS_TO_TICKS(100))==pdTRUE) {
            clearPage();invalidateScreen();
            pageText("Powering off",&inter_36,THEME_TEXT_WHITE,195);
            pageText("Release the button",&inter_24,THEME_TEXT_MUTED,265);
            lv_refr_now(NULL);xSemaphoreGive(lvgl_mutex);
        }
        powerOffDevice();
    }

}

// Button handler that's aware of setup mode - called by pollBootButton in lv_tick_task
static void handleNavigationButton() {
    if (splashActive) return;
    if (displayOff || displayManuallyOff || !displayAwake) { wakeDisplay(); return; }
    if (simCountdownActive) {
        closeCelebration();return;
    }
    if (simCelebrationActive || showingLandingSeq || showingBlockFoundScreen) {
        advanceCelebrationPage(true);return;
    }
    if (chargingBootMode) {
        // BOOT can peek at a sleeping charging display (handled above), but
        // starting the watch belongs to the lower Power button.
        return;
    }

    if (inSetupMode) {
        wakeDisplay();
        if (setupView == ui::SetupView::Qr || setupView == ui::SetupView::Manual) toggleSetupScreen();
        else if (setupView == ui::SetupView::Failed) handleSetupAction(ui::SetupAction::Retry);
    } else if (currentScreenIndex == NAV_SETTINGS && settingsView != ui::SettingsView::List) {
        settingsTappedAction = static_cast<int>(ui::PageAction::Back);
        triggerImmediateRedraw();
    } else {
        // waveshareDisplayToggle handles: wake if off, then cycle internal screen index
        waveshareDisplayToggle();
    }
}

void waveshareButtonClick(void) {
    navigationClickPending.store(true);
    triggerImmediateRedraw();
}

static void serviceBatteryNotice() {
    static lv_obj_t* notice=nullptr;
    static uint32_t shownAt=0,sampledAt=0;
    if(uint32_t(millis()-sampledAt)>=2000) {
        sampledAt=millis();const auto battery=getBatterySnapshot();
        batteryNotice.sample(battery.percent,battery.sample.usb,millis());
    }
    const bool eligible=!splashActive && !chargingBootMode && !inSetupMode &&
        !simCountdownActive && !showingLandingSeq && !showingBlockFoundScreen &&
        !displayOff && !displayManuallyOff && displayAwake;
    if(notice && (!eligible || uint32_t(millis()-shownAt)>=4000)) {
        lv_obj_delete(notice);notice=nullptr;
    }
    if(!eligible || notice || !batteryUserWake.exchange(false))return;
    const char* message=batteryNotice.takeLow(true) ? "Low battery. Charge soon." :
        batteryNotice.takeUnavailable(true,millis()) ? "Battery reading unavailable" : nullptr;
    if(!message)return;
    notice=lv_label_create(lv_layer_top());if(!notice)return;
    lv_label_set_text(notice,message);
    lv_obj_set_style_text_font(notice,&inter_24,0);
    lv_obj_set_style_text_color(notice,THEME_TEXT_WHITE,0);
    lv_obj_set_style_text_align(notice,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_bg_color(notice,lv_color_hex(0x151619),0);
    lv_obj_set_style_bg_opa(notice,LV_OPA_COVER,0);
    lv_obj_set_style_radius(notice,14,0);lv_obj_set_style_pad_all(notice,12,0);
    lv_obj_set_width(notice,350);lv_obj_align(notice,LV_ALIGN_BOTTOM_MID,0,-65);
    lv_obj_clear_flag(notice,LV_OBJ_FLAG_CLICKABLE);shownAt=millis();
}

// =============================================================================
// SCREEN DISPATCHER - 3 screens
// =============================================================================

void waveshareDisplay_ScreenDispatcher(unsigned long frame) {
    InputContextAfterDraw contextAfterDraw;
    serviceBatteryNotice();
    static bool was_in_setup_mode = false;
    // Catch a shutdown racing with wake; select Clock before the fresh frame
    // is flushed and the retained panel image becomes visible.
    applyBatterySaverScreenReset();

    if (simCountdownActive || showingLandingSeq || showingBlockFoundScreen) {
        updateSmartDimming();
        if (displayOff || displayManuallyOff) return;
    }

    // Priority 0: Simulation countdown
    if (simCountdownActive) {
        renderSimCountdown();
        if (simCountdownActive) return;
        // Expired: build the opening scene on this same pass, replacing "1".
    }

    updateCelebrationTour();
    updateDemoControls();

    // Priority 0a: Landing-sequence pre-roll (astronaut plants the flag) plays
    // first, then hands off to the block-found screen.
    if (showingLandingSeq) {
        renderLandingSequence();
        buildDemoControls();
        return;
    }
    // Visual tour: reward for six seconds, then QR. Music continues across pages.
    if (showingBlockFoundScreen) {
        if (celebrationPage) renderCertificate();
        else renderBlockFoundHouston();
        buildDemoControls();
        return;
    }

    // Priority 2: Hold splash screen for minimum duration
    if (splashActive && uint32_t(millis() - splashStartMs) < SPLASH_MIN_MS) {
        return;  // Let LVGL animate splash objects, don't switch screens yet
    }

    splashActive = false;

    // Wi-Fi success hands off to live connection state immediately.
    const bool setupExited = consumeSetupExit();
    if (wifiSetupConnected || setupExited) {
        wifiSetupConnected = false;
        wifiSetupSubmitted = false;
        setupConnectingDrawn = false;
        setupConnectFailed = false;
        inSetupMode = false;
        was_in_setup_mode = false;
        setupShowingDetails = false;
        currentScreenIndex = NAV_CLOCK;
        clearPage();
        invalidateScreen();
    }

    // Priority 3b: Show setup screen when waiting for WiFi config
    if (mMonitor.NerdStatus == NM_waitingConfig) {
        // Respect explicit PWR button sleep.
        if (displayManuallyOff) return;
        // If fully off (e.g. dimmed before setup began), wake to full brightness.
        if (displayOff) wakeDisplay();
        // Burn-in guard: the setup screen holds a bright, static, high-contrast
        // QR/steps image. Don't turn it fully OFF (the QR must stay scannable at
        // a glance), but after a minute idle, drop to dim brightness. Any touch
        // hits handleGesture's wake path and restores full brightness. Idle is
        // measured from lastWakeTime, refreshed by wakeDisplay() on every tap.
        #define SETUP_DIM_IDLE_MS 60000UL
        else if (displayAwake && (millis() - lastWakeTime > SETUP_DIM_IDLE_MS)) {
            displayAwake = false;
            setBrightness(DISPLAY_DIM_BRIGHTNESS);
        }

        // Finish any in-flight save on the portal task before closing it.
        if (setupView == ui::SetupView::Leaving) return;

        // If user submitted the portal form, show "Connecting..." screen
        if (wifiSetupSubmitted) {
            const WifiTargetSnapshot target = wifiTargetSnapshot();
            if (!setupConnectingDrawn || setupConnectingAttempt != target.setupAttempt) {
                // A retry (even the same SSID with a new password) gets a fresh
                // label and timeout; never leave the previous attempt on screen.
                drawSetupConnectingScreen(target);
            } else if (!wifiSetupConnected &&
                       (millis() - setupConnectingStart > SETUP_CONNECT_TIMEOUT_MS)) {
                // The network could not be joined. Drop
                // back to the setup screen with an error so the user can retry (the
                // portal underneath is still live and accepts a new submission),
                // instead of freezing on "Joining your WiFi..." forever.
                SERIAL_PRINTLN("[SETUP] WiFi connect timed out — back to setup screen");
                wifiSetupSubmitted = false;
                setupConnectingDrawn = false;
                setupConnectFailed = true;
                drawSetupQRScreen();
            }
            was_in_setup_mode = true;
            return;
        }

        // Setup mode - show QR code or details screen based on toggle
        if (!inSetupMode) {
            // First time entering setup - initialize
            splash_cleanup();
            inSetupMode = true;
            setupShowingDetails = false;
            drawSetupQRScreen();
        }
        was_in_setup_mode = true;
        // Redraw only when toggle was requested
        if (setupRedrawRequested) {
            setupRedrawRequested = false;
            if (setupShowingDetails) {
                drawSetupDetailsScreen();
            } else {
                drawSetupQRScreen();
            }
        }
        return;
    }

    // Clean up when exiting setup mode
    if (was_in_setup_mode && !inSetupMode) {
        SERIAL_PRINTLN("[DISPLAY] Exiting setup mode");
        inSetupMode = false;
        setupShowingDetails = false;
        setupConnectingDrawn = false;
        lv_obj_clean(lv_scr_act());  // Force full screen clear
        invalidateScreen();
        was_in_setup_mode = false;
    }

    // Priority 4: Normal screen navigation - clock shows connection status at bottom
    // Reset dim timer on first real screen render so it doesn't dim immediately after boot
    static bool firstScreenRendered = false;
    if (!firstScreenRendered) {
        firstScreenRendered = true;
        wakeDisplay();
    }

    // The clock screen will display connection progress in its status area
    switch (currentScreenIndex) {
        case NAV_MINING:
            waveshareDisplay_HomeScreen(frame);
            break;
        case NAV_CLOCK:
            waveshareDisplay_ClockScreen(frame);
            break;
        case NAV_DEVICE:
            waveshareDisplay_DeviceScreen(frame);
            break;
        case NAV_SETTINGS:
            waveshareDisplay_SettingsScreen(frame);
            break;
        default:
            waveshareDisplay_ClockScreen(frame);
            break;
    }
}

// =============================================================================
// SCREEN REGISTRATION
// =============================================================================

CyclicScreenFunction waveshareDisplayCyclicScreens[] = {
    waveshareDisplay_ScreenDispatcher,
    waveshareDisplay_ScreenDispatcher,
    waveshareDisplay_ScreenDispatcher,
    waveshareDisplay_ScreenDispatcher,
    waveshareDisplay_ScreenDispatcher,
};

DisplayDriver waveshareAmoled206Driver = {
    waveshareDisplay_Init,
    waveshareDisplay_AlternateScreenState,
    waveshareDisplay_AlternateRotation,
    waveshareDisplay_LoadingScreen,
    waveshareDisplay_SetupScreen,
    waveshareDisplayCyclicScreens,
    waveshareDisplay_AnimateScreen,
    waveshareDisplay_DoLedStuff,
    SCREENS_ARRAY_SIZE(waveshareDisplayCyclicScreens),
    0,
    WIDTH,
    HEIGHT
};

int applyAutoSwitch(int currentScreenIdx) {
    int forced = getAndClearForcedScreen();
    if (forced >= 0 && forced < SCREENS_ARRAY_SIZE(waveshareDisplayCyclicScreens)) {
        currentScreenIndex = forced;
        return forced;
    }
    return currentScreenIndex;
}

#endif
#ifdef WAVESHARE_AMOLED_206_DISPLAY
bool watchDiagnosticState(diagnostics::WatchState& state) {
    if (!lvgl_mutex || xSemaphoreTake(lvgl_mutex,pdMS_TO_TICKS(5)) != pdTRUE) return false;
    state.screen = (displayOff || displayManuallyOff) ? 0 : displayAwake ? 2 : 1;
    state.page = chargingBootMode ? 10 : inSetupMode ? 20 + int(setupView) : showingLandingSeq ? 30 : showingBlockFoundScreen ? (31 + celebrationPage) : currentScreenIndex;
    state.brightness = currentBrightness;
    state.demo = simCountdownActive ? 1 : simCelebrationActive ? 2 : 0;
    xSemaphoreGive(lvgl_mutex);
    return true;
}
#endif

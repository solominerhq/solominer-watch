// SOLOMINER modifications, 2026-10-20: GPL-3.0-only. See LICENSE and LICENSING.md.
// Existing upstream notices and permissions are retained.
#include "diagnostics/diagnostics.h"
#include "seriallog.h"
#include <Wire.h>

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include <OneButton.h>

#include "mbedtls/md.h"
#include "events.h"
#include "device/device_power.h"
#include "device/sleep_control.h"
#include "device/mining_control.h"
#include "network/wifi_connection.h"
#include "wManager.h"
#include "mining.h"
#include "monitor.h"
#include "drivers/displays/display.h"
#include "drivers/storage/SDCard.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"
#include "ShaTests/nerdSHA_HWTest.h"
#include "timeconst.h"

#include "freertos/semphr.h"

#ifdef WAVESHARE_AMOLED_206_DISPLAY
#include <lvgl.h>
#endif

#ifdef TOUCH_ENABLE
#include "TouchHandler.h"
#endif

#ifdef WAVESHARE_AMOLED_206_DISPLAY
extern void checkWaveshareButtons(void);
extern void waveshareButtonClick(void);
extern void waveshareDisplayToggle(void);
extern int getBatteryPercent(void);
extern bool isCharging(void);
extern bool isUSBConnected(void);
extern volatile bool chargingBootMode;
extern void chargingBootLoop(void);
extern bool isScreenActive(void);
extern void wakeDisplay(void);            // display only
#endif

#include <soc/soc_caps.h>
#include "hal/usb_serial_jtag_ll.h"
#include "esp_random.h"

// #define HW_SHA256_TEST

extern nvMemory nvMem;      // defined in wManager.cpp
extern TSettings Settings;  // defined in wManager.cpp

// Global flag: true when running on battery with no USB host connected.
// When set, Serial output is suppressed to save ~5-10 mA from USB CDC PHY.
std::atomic<bool> g_serialDisabled{false};

// 3 seconds WDT
#define WDT_TIMEOUT 3
// 15 minutes WDT for miner task
#define WDT_MINER_TIMEOUT 900

#ifdef PIN_BUTTON_1
OneButton button1(PIN_BUTTON_1);
#endif

#ifdef PIN_BUTTON_2
OneButton button2(PIN_BUTTON_2);
#endif

#ifdef TOUCH_ENABLE
extern TouchHandler touchHandler;
#endif

// LVGL mutex - defined in waveshare display driver.cpp
extern SemaphoreHandle_t lvgl_mutex;

// Touch input handler - defined in waveshare display driver.cpp
extern void updateTouchInput(void);

extern monitor_data mMonitor;

#ifdef SD_ID
SDCard SDCrd = SDCard(SD_ID);
#else
SDCard SDCrd = SDCard();
#endif

/**********************Ã¢Å¡Â¡ GLOBAL Vars *******************************/

unsigned long start = millis();
const char *ntpServer = "pool.ntp.org";

// void runMonitor(void *name);

/********* INIT *****/
void setup()
{
  // Init pin 15 to eneble 5V external power (LilyGo bug)
#ifdef PIN_ENABLE5V
  pinMode(PIN_ENABLE5V, OUTPUT);
  digitalWrite(PIN_ENABLE5V, HIGH);
#endif

  // Boot at 80 MHz. This is also the battery floor: the ESP32-S3 WiFi radio
  // requires the APB bus at 80 MHz, so we never go below it while WiFi is on.
  // The final profile is applied after PMU + WiFi init (see below).
  setCpuFrequencyMhz(80);

  // Seed the C RNG from the ESP32 hardware RNG. Without this, every unit runs
  // the identical rand() sequence, so the stratum reconnect backoff (rand() % 60)
  // is the same on every device — a fleet would reconnect in lockstep after a
  // pool blip. esp_random() gives real entropy and works before WiFi is up.
  srand(esp_random());

#ifdef MONITOR_SPEED
  Serial.begin(MONITOR_SPEED);
#else
  Serial.begin(115200);
#endif // MONITOR_SPEED

  Serial.setTimeout(0);
  delay(SECOND_MS / 10);

  // Detect if USB host is connected. If running on battery with no USB,
  // disable serial output to save ~5-10 mA from the USB CDC PHY.
  // The USB Serial JTAG peripheral sets SOF (Start-of-Frame) when a host
  // is actively communicating; if no frames received, no host is attached.
  #ifdef ARDUINO_USB_CDC_ON_BOOT
  {
    // Give USB a moment to enumerate if cable was just plugged in
    delay(200);
    bool usbConnected = usb_serial_jtag_ll_txfifo_writable();
    if (!usbConnected) {
      g_serialDisabled = true;
      Serial.end();  // Powers down USB CDC peripheral
    }
  }
  #endif

  if (!g_serialDisabled) {
    // Print actual CPU frequency to verify the change worked
    SERIAL_PRINTLN("===========================================");
    SERIAL_PRINT("CPU Frequency set to: ");
    SERIAL_PRINT(getCpuFrequencyMhz());
    SERIAL_PRINTLN(" MHz");
    SERIAL_PRINTLN("===========================================");
  }

  esp_task_wdt_init(WDT_MINER_TIMEOUT, true);
  // Idle task that would reset WDT never runs, because core 0 gets fully utilized
  disableCore0WDT();
  // disableCore1WDT();

#ifdef HW_SHA256_TEST
  while (1)
    HwShaTest();
#endif

// Setup the buttons
#if defined(PIN_BUTTON_1) && !defined(PIN_BUTTON_2) // One button device
  #ifdef WAVESHARE_AMOLED_206_DISPLAY
  button1.attachClick(waveshareDisplayToggle);  // Single tap: toggle display on/off
  #else
  button1.setPressMs(5 * SECOND_MS);
  button1.attachClick(switchToNextScreen);
  button1.attachDoubleClick(alternateScreenRotation);
  button1.attachLongPressStart(reset_configuration);
  button1.attachMultiClick(alternateScreenState);
  #endif
#endif

#if defined(PIN_BUTTON_1) && defined(PIN_BUTTON_2) // Button 1 of two button device
  button1.setPressMs(5 * SECOND_MS);
  button1.attachClick(alternateScreenState);
  button1.attachDoubleClick(alternateScreenRotation);
#endif

#if defined(PIN_BUTTON_2) // Button 2 of two button device
  button2.setPressMs(5 * SECOND_MS);
  button2.attachClick(switchToNextScreen);
  button2.attachLongPressStart(reset_configuration);
#endif

  /******** INIT SOLOMINER ************/
  SERIAL_PRINTLN("SoloMiner starting......");

  /******** LOAD SAVED SETTINGS (before display init so brightness/timeouts apply) *****/
  nvMem.loadConfig(&Settings);
  const bool setupBootRequested = isSetupBootRequested();
  device::requestBatterySaver(Settings.batterySaver);
  diagnostics::begin();

  /******** APPLY TIMEZONE (before display init's RTC seed) *****/
  // Sets the C-library TZ so localtime()/mktime() resolve local time with DST.
  // Must run before initDisplay() below, whose RTC seed uses mktime() and whose
  // NTP→RTC sync uses localtime() — both need the zone already in place.
  applyTimezone();

  /******** INIT DISPLAY ************/
  initDisplay();

  /******** CHARGING BOOT CHECK *****/
  // If the device powered on because USB was plugged in (not a PWR button press),
  // show a charging screen instead of fully booting. Lower/PWR starts the watch.
  // An explicit setup restart must reach the QR page even with USB attached.
#ifdef WAVESHARE_AMOLED_206_DISPLAY
  if (isUSBConnected() && !setupBootRequested) {
    SERIAL_PRINTLN("[BOOT] USB power detected at boot — entering charging screen");
    chargingBootMode = true;
    chargingBootLoop();  // Blocks until an awake lower/Power-button tap
    SERIAL_PRINTLN("[BOOT] Charging boot exited — continuing normal boot");
  }
#endif

  /******** PRINT INIT SCREEN *****/
  if (!setupBootRequested) drawLoadingScreen();
  // The monitor animates the brief splash while startup continues below.
  // Don't delay starting the network for the astronaut reveal.

  /******** SET STATUS BASED ON STORED CONFIG *****/
  // If config exists: show clock screen while WiFi connects in background
  // If no config: show setup screen (wManager will handle the portal)
  if (!setupBootRequested && hasStoredConfig()) {
    mMonitor.NerdStatus = NM_Connecting;
  } else {
    mMonitor.NerdStatus = NM_waitingConfig;
  }
  doLedStuff(0);

#ifdef SDMMC_1BIT_FIX
  SDCrd.initSDcard();
#endif

  /******** CREATE TASK TO PRINT SCREEN *****/
  // Start monitor before WiFi init so the screen updates during connection
  SERIAL_PRINTLN("");
  SERIAL_PRINTLN("Initiating tasks...");
  static const char monitor_name[] = "(Monitor)";
#if defined(CONFIG_IDF_TARGET_ESP32)
  // Increased stack for ESP32 classic due to NVS operations
  BaseType_t res1 = xTaskCreatePinnedToCore(runMonitor, "Monitor", 9500, (void *)monitor_name, 5, NULL, 1);
#else
  BaseType_t res1 = xTaskCreatePinnedToCore(runMonitor, "Monitor", 10000, (void *)monitor_name, 5, NULL, 1);
#endif

  /******** INIT WIFI ************/
  network::begin();

  // If configModeCallback fired during WiFi init (set NM_waitingConfig) but WiFi
  // ultimately connected (e.g. user re-entered creds via portal), restore NM_Connecting
  // so the Monitor task transitions away from the setup screen.
  if (WiFi.status() == WL_CONNECTED && mMonitor.NerdStatus == NM_waitingConfig) {
    mMonitor.NerdStatus = NM_Connecting;
  }

  // Power policy has one owner; the opt-in saver uses physical input activity.
  device::beginPower(isUSBConnected());


  /******** CREATE STRATUM TASK *****/
  static const char stratum_name[] = "(Stratum)";
#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(ESP32_2432S028R) && !defined(ESP32_2432S028_2USB)
  // Reduced stack for ESP32 classic to save memory
  BaseType_t res2 = xTaskCreatePinnedToCore(runStratumWorker, "Stratum", 12000, (void *)stratum_name, 4, NULL, 1);
#elif defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
  // Free a little bit of the heap to the screen
  BaseType_t res2 = xTaskCreatePinnedToCore(runStratumWorker, "Stratum", 13500, (void *)stratum_name, 4, NULL, 1);
#else
  BaseType_t res2 = xTaskCreatePinnedToCore(runStratumWorker, "Stratum", 15000, (void *)stratum_name, 4, NULL, 1);
#endif

  /******** CREATE MINER TASKS *****/
  // for (size_t i = 0; i < THREADS; i++) {
  //   char *name = (char*) malloc(32);
  //   sprintf(name, "(%d)", i);

  // Start mining tasks
  // BaseType_t res = xTaskCreate(runWorker, name, 35000, (void*)name, 1, NULL);
  TaskHandle_t minerTask1, minerTask2 = NULL;
#ifdef HARDWARE_SHA265
#if defined(CONFIG_IDF_TARGET_ESP32)
  xTaskCreate(minerWorkerHw, "MinerHw-0", 3584, (void *)0, 3, &minerTask1); // Reduced for ESP32 classic
// xTaskCreate(minerWorkerSw, "MinerSw-0", 5000, (void*)0, 1, &minerTask1); // Reduced for ESP32 classic
#else
  xTaskCreate(minerWorkerHw, "MinerHw-0", 4096, (void *)0, 3, &minerTask1);
#endif
#else
#if defined(CONFIG_IDF_TARGET_ESP32)
  xTaskCreate(minerWorkerSw, "MinerSw-0", 5000, (void *)0, 1, &minerTask1); // Reduced for ESP32 classic
#else
  xTaskCreate(minerWorkerSw, "MinerSw-0", 6000, (void *)0, 1, &minerTask1);
#endif
#endif
  esp_task_wdt_add(minerTask1);

#if (SOC_CPU_CORES_NUM >= 2)
  // Always create SW worker — it idles automatically when g_singleCoreMode is true
  // (no SW jobs are dispatched). Toggled dynamically: battery = single, USB = dual.
#if defined(CONFIG_IDF_TARGET_ESP32)
    xTaskCreate(minerWorkerSw, "MinerSw-1", 5000, (void *)1, 1, &minerTask2);
#else
    xTaskCreate(minerWorkerSw, "MinerSw-1", 6000, (void *)1, 1, &minerTask2);
#endif
    esp_task_wdt_add(minerTask2);
#endif

  vTaskPrioritySet(NULL, 4);

  onMiningStarted();

  /******** MONITOR SETUP *****/
  setup_monitor();
}

void app_error_fault_handler(void *arg)
{
  // Get stack errors
  char *stack = (char *)arg;

  // Print the stack errors in the console
  esp_log_write(ESP_LOG_ERROR, "APP_ERROR", "Error Stack Code:\n%s", stack);

  // restart ESP32
  esp_restart();
}

void loop()
{
  device::lightSleepCheckpoint(device::SleepRole::Main);
#ifdef WAVESHARE_AMOLED_206_DISPLAY
  bool screenOn = isScreenActive();

  device::servicePower(isUSBConnected());
#else
  bool screenOn = true;
#endif

  // Skip LVGL and touch/button processing when screen is off — saves ~2-3 mA.
  // Button wake and touch wake are handled by lv_tick_task independently.
  if (screenOn) {
    // All LVGL calls (timer handler, button handlers, touch input) share the same
    // mutex acquisition so the Monitor task cannot interleave LVGL operations.
    // 100ms timeout: ensures button1.tick() fires even when monitor task
    // holds the mutex during screen rendering (QR code etc. can take >10ms)
    if (lvgl_mutex != NULL && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      lv_timer_handler();

#if defined(PIN_BUTTON_1) && !defined(WAVESHARE_AMOLED_206_DISPLAY)
      // Waveshare AMOLED: button polling is handled in lv_tick_task (runs during WiFiManager portal)
      button1.tick();
#endif

#ifdef PIN_BUTTON_2
      button2.tick();
#endif

#ifdef TOUCH_ENABLE
      touchHandler.isTouched();
#endif

      updateTouchInput();

      xSemaphoreGive(lvgl_mutex);
    }
  }



  // When screen is off, slow down the main loop to save CPU power (~1-2 mA).
  // Button/touch wake is handled by lv_tick_task, so 200ms is responsive enough.
  vTaskDelay(screenOn ? (50 / portTICK_PERIOD_MS) : (200 / portTICK_PERIOD_MS));
}

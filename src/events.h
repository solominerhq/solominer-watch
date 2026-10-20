#pragma once

// =============================================================================
// SoloMiner Event Callbacks
// =============================================================================
//
// These functions are called automatically by SoloMiner when key mining events
// occur. They are "weak" by default — empty and safe to ignore. To handle an
// event, create a new .cpp file in src/ and define any of these functions.
//
// IMPORTANT: These callbacks run inside FreeRTOS mining tasks on the ESP32.
//   - Keep them short and non-blocking
//   - Do not call delay() or anything that blocks
//   - Do not write to the display directly
//   - Queue any networking/display work for its owner task.
//   - Never change Wi-Fi state or perform blocking HTTP/MQTT calls here.
//
// -----------------------------------------------------------------------------
// EXAMPLE — create src/my_callbacks.cpp with this content:
// -----------------------------------------------------------------------------
//
//   #include "events.h"
//   #include <Arduino.h>
//
//   void onBlockFound() {
//       // A valid block solution was confirmed by the pool.
//       // Odds are extremely low, but if it happens — make something happen.
//       digitalWrite(2, HIGH);  // turn on an LED
//   }
//
//   void onShareAccepted(double difficulty) {
//       // Pool accepted a share. This happens regularly during normal mining.
//       Serial.printf("Share accepted! Difficulty: %.2f\n", difficulty);
//   }
//
// =============================================================================

// Called when the pool confirms a valid block solution was found.
// This is the "lottery win" event. Difficulty will be very high.
void onBlockFound();

// Called when the pool accepts a submitted share.
// Happens regularly during normal operation.
// difficulty: the difficulty value of the accepted share
void onShareAccepted(double difficulty);

// Called when the pool rejects a submitted share.
void onShareRejected();

// Called when a new mining job is received from the pool.
// The device switches to the new job immediately.
void onNewJobReceived();

// Called once when the mining tasks start up after boot.
void onMiningStarted();

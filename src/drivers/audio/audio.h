#ifndef BLOCKFOUND_AUDIO_MODULE_H
#define BLOCKFOUND_AUDIO_MODULE_H
#include <stdint.h>

// Block-found celebration audio (ES8311 codec + I2S on the Waveshare 2.06).
// Initialized lazily, then retained until reboot for reliable replay.
// Default/public builds contain no recording; prewarm/play are then no-ops.
bool audioHasBlockFoundTrack();

// Bring the codec + I2S up early (e.g. as the celebration is requested) so a
// following audioPlayBlockFound() starts instantly instead of paying the one-time
// hardware init. Safe to call repeatedly; no-op once the hardware is up.
void audioPrewarm();

// Start the celebration track. Idempotent (ignored if already playing).
// Spawns a short-lived FreeRTOS task; returns immediately.
void audioPlayBlockFound();

// Stop playback early (e.g. the celebration was dismissed). Safe to call anytime.
void audioStopBlockFound();

// Time since the first decoded PCM frame was accepted by I2S. False before playback
// starts (or if audio failed); the display can then use its fallback clock.
// After stopping, returns the last track's final position until the next
// audioPlayBlockFound() request. A new UI session must not read it before that.
bool audioBlockFoundPositionMs(uint32_t& position);

// Silence the speaker while decoding/animation timing continue uninterrupted.
void audioSetBlockFoundMuted(bool muted);
bool audioBlockFoundMuted();
bool audioIsPlaying();
bool audioHardwareReady();

// Pair only after a successful prepare. Playback resumes DMA lazily on demand.
bool audioPrepareLightSleep();
void audioFinishLightSleep();

#endif // BLOCKFOUND_AUDIO_MODULE_H

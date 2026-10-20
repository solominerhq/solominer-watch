# Light-sleep hardware probe

This is separate diagnostic firmware for the Waveshare 2.06-inch watch. It tests
real processor light sleep, timer/button/touch wake-up, and repeated Wi-Fi
reconnections. It does not mine, connect to a pool, play audio, or run the normal
watch interface. This diagnostic build is separate from normal watch firmware.

## Install

Use `SOLOMINER-light-sleep-test_firmware.bin` as an **application update** through
your usual watch firmware-update workflow. It uses the existing 32 MB partition
layout. Do not erase flash or overwrite NVS/SPIFFS: the test needs the Wi-Fi
credentials already saved by the normal firmware. There is no setup portal in
the probe. If it reports no saved Wi-Fi, restore normal firmware, configure the
iPhone hotspot, and then install the probe again.

The screen says SLEEP TEST so this cannot be confused with customer firmware.
Use a test watch with more than 10% battery; starting with a full charge is best.
Record results against the exact probe build and board revision.

## Three tests

1. **Touch wake:** unplug USB and briefly press BOOT. Release it during the
   five-second countdown. When the screen turns off, wait two seconds and tap
   it. It should wake and show “Input woke watch.” The run stops intentionally.
   Plug in, wait ten seconds, and export the results.
2. **Button wake:** repeat, using a brief PWR press when the screen is asleep.
   Again, an input wake deliberately ends the run. Export afterward. BOOT can
   also wake it, but the touch/PWR tests match normal daily use more closely.
3. **Timer and hotspot endurance:** lock the iPhone and leave it locked. Unplug
   the watch and press BOOT again. Leave the watch untouched. It tests 10 seconds,
   60 seconds, then twenty three-minute sleeps. Allow about 75 minutes. After
   each timer wake it rejoins saved Wi-Fi, confirms five continuous seconds
   connected, and returns to sleep with the screen off. Completion displays
   “22 cycles complete”; that display itself turns off after the idle timeout.
   Plug in and export the results. Do not open iPhone Hotspot settings during
   the test: doing so would change the condition we are trying to measure.

A timeout, unexpected wake, held wake pin, low/unknown battery, or log-write
failure stops the sequence. There are no unbounded reconnect attempts. A Wi-Fi
join has a 20-second budget; the five-second dwell begins after connection.
No Internet ping, pool connection, or mining work is sent during this test.
The five-second dwell is an experimental starting value, not an Apple guarantee.

**USB does not have a dedicated wake interrupt in this prototype.** Plugging in
may leave it asleep until the next timer wake (up to three minutes). Briefly
press PWR to wake immediately, then wait ten seconds before downloading. USB
power prevents a new test and ends an active one once the processor wakes.

## Export

Close other serial monitors and open
**scripts/Download sleep test.command** on your Mac. The helper uses Python 3, with a
fallback to PlatformIO's Python; no extra Python packages are required.

It saves `SOLOMINER-sleep-test-<timestamp>.zip` in Downloads. Review and sanitize the archive before sharing.
It includes the original JSONL, a per-cycle summary and checksum verification.
If macOS blocks the launcher, run `python3 scripts/download_light_sleep_test.py` from
the repository root. macOS/Linux USB are supported; Windows transport is not.

The exporter reads only `/light-sleep-test.jsonl`; normal diagnostic logs and
configuration are untouched. Export does not delete any logs. The 128 KiB probe
log is append-only and stops accepting tests when full or filesystem headroom
falls below 256 KiB. Do not erase flash to clear it. A sudden power loss can
leave a torn final row; the helper flags it and preserves the original bytes.
The test closes and flushes each record before sleeping. No credentials, SSID,
wallet or pool address is written to the probe log.

## What the results mean

- `sleep_enter` without `sleep_exit`, followed by a new boot: investigate reset,
  power loss or missing data. The log includes reset reason on each record.
- `sleep_exit`: planned/actual elapsed time, ESP sleep result and wake cause.
  Timer is 4, GPIO is 7. Pin mask: 1 BOOT, 2 PWR, 4 touch; a released pin may
  already read inactive when sampled. An intentional GPIO wake ends the run.
- `rejoined`: time from starting Wi-Fi to a connected station, followed by a
  successful five-second connected dwell; also records signal strength.
- Join error 1: timed out or connection lost during dwell; 2: user/USB stop;
  3: battery too low or unreadable. Intentional interruptions are not evidence
  of a hotspot failure.
- A complete sequence requires all 22 timer/rejoin cycles with valid sleep
  durations and an explicit completion record. The helper does not mark an
  incomplete, rejected, duplicate or USB-interrupted sequence as successful.
- Raw battery percentage and millivolts are context, **not measured current**.
  There is no production-runtime estimate from this stripped-down firmware.

Repeat successful endurance runs with other iPhones/iOS versions, different
signal strength and phone power conditions to compare reconnection behavior.
The probe timings are experimental, not guarantees about hotspot availability. Testing Wi-Fi reconnection does not yet test pool authorization or
how quickly fresh mining work can resume after interaction.

## Restore the watch

Install a known-good normal application image for this board without erasing
flash. The probe does not change the saved Battery Saver preference.

The production firmware already uses cooperative light sleep. It leaves Wi-Fi
stopped during Saver idle; this probe's periodic Wi-Fi reconnection loop is a
diagnostic experiment, not the production policy. Test production behavior using
[the hardware checklist](HARDWARE_TESTS.md).

## Build and validation

From the main project directory:

`pio run -c platformio.sleep-test.ini`

The separate config uses `tests/hardware/light_sleep` as its source directory
and `.pio/build-light-sleep` for artifacts. It retains the pinned Arduino/ESP-IDF
stack and hardware configuration. Explicit light sleep does not require enabling
automatic light sleep/tickless idle. Flash/PSRAM use the default sleep policy;
there are no aggressive flash-power changes.

The probe has one application loop. Unlike the production firmware, it has no
concurrent mining/LVGL/audio/recorder tasks holding hardware resources at sleep
entry. Passing this probe is a prerequisite, not proof that production sleep
integration is safe. Speaker amplification is held off; the audio codec is not
initialized. Battery protection ends tests below the start threshold and checks
for critically low battery while idle.

The firmware build and 14 host tests passed, including the existing checksum/USB
transport tests and new complete/incomplete, GPIO, short-sleep, error, duplicate
and torn-log summary checks. Physical sleep, display/touch/button wake-up, USB
re-enumeration and hotspot endurance still require your watch tests.

References:
- [Espressif ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/v4.4.3/esp32s3/api-reference/system/sleep_modes.html)
- [Apple Personal Hotspot troubleshooting](https://support.apple.com/en-us/119837)

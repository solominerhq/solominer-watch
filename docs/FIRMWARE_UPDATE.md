# Update your SoloMiner Watch

These instructions apply to a SoloMiner Watch built on the Waveshare
ESP32-S3-Touch-AMOLED-2.06 with the supported 32 MB partition layout.
Do not use this firmware on other boards or an unknown partition layout.

## Before you start

- Download `SOLOMINER-v3.1.0.bin` to your phone's Files or Downloads folder
  **before** connecting to the watch's setup network.
- Plug the watch into USB power and keep it connected until the update finishes.
- Note your Wi-Fi, pool, Bitcoin receiving address, and timezone settings.
  Routine updates are designed to preserve them.
- This public build has no celebration music. Installing it replaces any
  recording embedded in the previous firmware; the animation still plays.

Use the application `.bin` above. The factory image and source ZIP are not
files for the phone updater.

## Install

1. On the watch, open **Settings → Wi-Fi & Setup → Open Setup**.
   Older firmware may use different labels for Setup.
2. Join the watch's setup Wi-Fi from your phone. Scan the watch's QR code or
   use the network details it shows. Stay connected even if your phone reports
   that this network has no internet.
3. Open the setup page. If it does not appear, open Safari or Chrome and visit
   `http://192.168.4.1`.
4. Open **Firmware update**, choose `SOLOMINER-v3.1.0.bin`, and start the upload.
   If the captive browser cannot select a file, use Safari or Chrome at
   `http://192.168.4.1/update`.
5. Keep the page open and the watch powered. Wait for the success message and
   restart; do not disconnect it while the upload is running.
6. If the charging screen appears after restart, press the lower **PWR** button
   to start the watch. Check that the Device screen reports **v3.1.0**.

Turn your phone's hotspot back on if that is the watch's saved network. Allow
time for Wi-Fi and pool reconnection.

## Check the update

Confirm the clock/timezone, saved settings, Wi-Fi connection, and live mining
statistics. Try swiping between screens and using the Settings controls.
Hold the top **BOOT** button for five seconds to start the Block Found demo.
Check immediate page navigation and Close on all three pages, then launch it
again to confirm it starts from the beginning. The silent build has no sound
button or music.

If an upload fails, keep the watch powered, reconnect to its setup network, and
retry with the application file. If it cannot open Setup, see
[USB flash and recovery](USER_GUIDE.md#flash-and-recovery).

Developers can add a recording through the [custom audio instructions](CUSTOM_AUDIO.md).

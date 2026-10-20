# SoloMiner Watch v3.0.0

**Public source preview for the Waveshare ESP32-S3-Touch-AMOLED-2.06.**
This repository provides source to build and customize. Prebuilt firmware releases
remain subject to the [release checklist](OPEN_SOURCE_RELEASE.md).

## Everyday use

- Clock, mining statistics, Device, and Settings screens with swipe/button navigation.
- Three large Settings rows: Brightness, Battery Saver, and Wi-Fi & setup.
- Wi-Fi status dots are orange, pool status dots blue, and active mining green.
- Clock and startup share a static starfield. Startup shows stars for 250 ms,
  then the astronaut for 1.25 seconds before opening Clock or Setup.
- Charging shows **Reading battery level...** until a validated percentage is
  available. Unknown readings are never presented as an invented percentage.

## Mining and battery

- The same active mining profile on USB and battery: 80 MHz, one hardware
  worker, and a 16 ms gap between batches.
- Battery Saver is on by default. Three minutes of inactivity on battery pauses
  mining and Wi-Fi, returns navigation to Clock, and permits cooperative light sleep.
- Physical interaction resumes connection and mining. USB bypasses the Saver
  inactivity timer; thermal and low-battery protections still apply.
- Waking can request an earlier Wi-Fi retry. Newly connected sessions discard
  stale hashrate samples until fresh work is available.
- A successful Wi-Fi connection can wake an automatically dark display when
  eligible; manual screen-off and Saver sleep are respected.

## Setup and reliability

- Phone setup runs locally on the watch, with no account or external page assets.
- Wi-Fi, receiving address, timezone, and clock format share one page; pool
  options are under Advanced. Existing passwords are never sent to the phone.
- Settings writes preserve the previous configuration on failure. Statistics
  use a versioned, checksummed record and are checkpointed periodically.
- Hold both buttons for five seconds, then release, to restart into setup
  without erasing settings or statistics. A completely frozen application may
  still require USB download-mode recovery.

## Block Found

- Astronaut landing and flag strike, white/orange headings, reward details,
  and a stationary explorer QR code.
- One automatic page tour; swiping or pressing BOOT takes manual control.
- A three-second cancellable demo countdown. The demo uses an example block
  and never alters the real block count or saved block record.
- Silent by default. Developers can [supply their own music](CUSTOM_AUDIO.md)
  without editing player code. With a track, volume defaults to 69/100 and the
  complete recording plays across pages unless the user exits.
- Replay resets the animation and audio state. Sound and close controls use
  separate inset backgrounds and generous touch targets.

## Build and compatibility

The supported board and 32 MB partition layout are unchanged. Routine application
updates preserve configuration; legacy statistics migrate when valid. Back up
settings before flashing and use the [user guide](USER_GUIDE.md) for installation.

Automated checks cover portable policies, persistence, setup, controls, audio,
and source packaging. Physical-device acceptance is a separate release step;
a successful build is not a battery-runtime or hardware-reliability guarantee.

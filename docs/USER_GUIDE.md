# SoloMiner Watch user guide

Bitcoin solo-mining watch firmware for the **Waveshare ESP32-S3-Touch-AMOLED-2.06**.
Built on [NerdMiner v2](https://github.com/BitMaker-hub/NerdMiner_v2).

The watch connects to a compatible Stratum pool, performs SHA-256 hashing, and
shows the clock and mining statistics. It is a mining client, not a Bitcoin full
node. Rewards are extremely unlikely; its purpose is education and enjoyment.

## Current firmware version

**v3.0.0** supports one watch and one build target: SOLOMINER on the
Waveshare ESP32-S3-Touch-AMOLED-2.06.

See the [v3.0.0 release notes](RELEASE_v3.0.0.md) for this source preview.
Routine application updates preserve saved settings.

Setup saves the timezone, clock format, wallet and pool settings immediately
when Save is pressed, before trying to connect to Wi-Fi. Timezone-only changes
survive a restart even while the setup portal is still open. The selected
timezone is applied immediately, including its daylight-saving rules.

- Same active mining profile on battery and USB: 80 MHz, one hardware worker,
  and a 16 ms pause between batches. Thermal and low-battery protection remain.
- Battery Saver is on by default. After three minutes without physical input on
  battery, it stops mining and Wi-Fi and uses cooperative light sleep. Touch or
  a button wakes it. No hotspot keepalive or periodic Wi-Fi reconnection runs
  while paused. USB and Saver Off allow continuous mining.
- When Battery Saver shuts Wi-Fi down, it returns navigation to Clock without
  lighting the display. The next wake opens Clock. Ordinary display timeout or
  manual screen-off without a Saver shutdown keeps the current page.
- The first successful Wi-Fi connection in an active window can grant another
  three minutes, once per physical interaction (also available on initial boot).
  Reconnects cannot keep extending it. An expired window cannot be revived by
  a late connection event. Unplugging grants its own fresh window.
- A Wi-Fi connection can wake an automatically dark screen, with a 30-second
  cooldown. Manual screen-off and idle Saver are respected.
  A connection arriving during the display eligibility check remains pending
  for the next display tick rather than losing its wake notification.
- Settings has three large rows: Brightness, Battery Saver, and Wi-Fi & setup.
  Brightness opens Low / Medium / High choices and applies the selection immediately.
  Battery Saver has an On / Off switch, green when enabled, with a short explanation.
  A header arrow, right swipe or BOOT returns from a detail screen. On the main
  Settings list, BOOT still cycles to Clock. Changes save without a Done button.
- Phone setup explains what can be changed before opening the portal. Scan to
  connect joins the watch's Wi-Fi; Manual shows the network, password and address.
  Exit setup closes the portal and returns to Clock, preserving saved settings.
  A save or firmware upload already in progress finishes before exit is handled.
  Block Found Demo remains available by holding BOOT, not from the Settings list.
- The clock status has a fixed baseline and optional guidance beneath it.
  Wi-Fi indicators are orange, pool indicators blue, and active mining green.
  After 30 seconds of active Wi-Fi attempts, network/hotspot guidance appears
  without changing the dot color. Saver sleep clears the warning.
  Green Mining requires a fresh positive hashrate; Battery Saver is neutral gray.
- Clock has sparse, static star accents around its clear text areas. The other
  daily screens keep their original backgrounds; no star animation or timer runs.
- Clock stars continue behind the transparent connection/mining status rows,
  including the network subtitle, without an empty strip or new animation.
- The astronaut splash opens with 250 ms of stars, shows the astronaut instantly
  for 1.25 seconds, then switches directly to Clock or Setup (1.5 seconds total).
  Splash and Clock share the same static stars so the background stays in place.
  Startup work continues in parallel. The initial USB charging screen has
  dimmer accents outside its battery ring and power-button instruction.
  While the battery reading settles, Reading battery level... appears inside
  the dim ring with a slowly rotating orange arc. A valid percentage replaces
  the message and fills the green ring to the actual level; the number does
  not count up through invented readings.
- Block Found has three swipeable pages: celebration, reward, and block explorer
  QR. Once the astronaut settles, hold BLOCK FOUND for eight seconds, then show
  the reward for six seconds before holding the QR page;
  a developer-supplied recording continues across pages until it finishes or the
  user exits. The default build is silent.
  Swiping or pressing BOOT takes manual control; the QR page holds. The demo leaves real counters intact.
  Titles alternate white/orange during playback and settle into orange when
  playback finishes. Numbers stay steady and the QR remains black and white.
- Hold BOOT for five seconds to open BLOCK FOUND DEMO with a three-second
  countdown. Sound, page dots and close each have their own dark inset background.
  Page dots appear two beats after the flag strike. The demo uses a historical example block and QR.
- BLOCK REWARD shows the base subsidy in BTC, not transaction fees or a
  guaranteed pool payout. YOUR BLOCK shows the height; SEE YOUR BLOCK contains the explorer QR.
  There is no USD conversion, price lookup, or price-related background task.

Saved settings are preserved. Battery runtime and hotspot recovery still need
hardware testing; no all-day battery-life claim is established.

Before distributing firmware binaries, complete [release preparation](OPEN_SOURCE_RELEASE.md).
The linked binary-dependency review and physical acceptance are separate from
publication of application source.

## Hardware

- Waveshare ESP32-S3-Touch-AMOLED-2.06, 410 × 502 display
- 32 MB flash, 8 MB PSRAM, AXP2101 power management and RTC
- 2.4 GHz Wi-Fi, USB-C, PWR/BOOT buttons and touch navigation

Other boards are not supported by this build, even where inherited code contains
older board conditionals. See [Waveshare hardware documentation](https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm).

## Build

Install Python 3.11 and Git. A source ZIP also works: extract it and run the same
commands from the extracted directory. No Git tags are required.

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-build.txt
pio pkg install -e Waveshare-AMOLED-206
pio run -e Waveshare-AMOLED-206
```

On Windows, activate with `.venv\Scripts\activate` and use `python` in place of
`python3`. The first build downloads dependencies. Their versions are pinned in
`platformio.ini`; generated library caches do not belong in the repository.

Outputs are under `firmware/<version>/`:

For sharing through the watch portal, use `SOLOMINER-<version>.bin`. This is identical to the application
images below and are generated automatically on each firmware build.

| File | Purpose |
|---|---|
| `Waveshare-AMOLED-206_factory.bin` | Factory image for this partition layout, offset `0x0` |
| `Waveshare-AMOLED-206_firmware.bin` | Application image; use the normal PlatformIO upload or a compatible update process |
| `Waveshare-AMOLED-206_manifest.json` | Version, file sizes, SHA-256 checksums |

`src/version.h` is the version source for the display and artifact directory.
There is no alternate branding target.

## Flash and recovery

Connect a USB data cable and run:

```sh
pio run -e Waveshare-AMOLED-206 -t upload
pio device monitor
```

If automatic detection fails, specify `--upload-port` with the device's serial
port. To enter the ROM downloader, hold BOOT while resetting/powering on the
board, then release BOOT and retry. Confirm your board's button sequence in its
hardware documentation. A charge-only cable cannot upload firmware.

For web/manual flashing, use the factory image at `0x0` only with this supported
hardware and partition layout. A raw application write to `0x10000` is appropriate
only when app0 is the selected boot slot; it is not a universal OTA update command.
Do not erase flash during a routine upgrade if you want to retain configuration.

USB connection at power-on may show the charging screen first. Press and release the lower (PWR) button to
start the watch. If the charging display is dark, the first press wakes it;
press and release the lower button again to start.

## First setup

Join the watch's setup network (normally `SOLOMINER`, password `minebitcoin`),
then open its captive portal or `http://192.168.4.1`. Enter a Bitcoin mainnet
receiving address, Wi-Fi credentials and timezone. The default endpoint is
`public-pool.io:21496`; use a compatible pool accepting low-difficulty shares.
Availability and pool policies can change.

The branded phone page groups Wi-Fi, your Bitcoin address and the clock;
pool settings are under Advanced. Saved passwords and timezones are preserved.
See [phone setup implementation and testing](PHONE_SETUP.md) for editing
the offline page or verifying it on a watch.

The firmware validates the wallet checksum and waits for the pool to authorize
the worker before using mining work. `Check pool login` means to check pool,
wallet/worker and pool password, rather than repeatedly changing Wi-Fi settings.

### Using an iPhone hotspot

1. On the iPhone, enable Personal Hotspot → Allow Others to Join. On supported
   iPhones, enable Maximize Compatibility for the watch's 2.4 GHz connection.
2. Note the hotspot name and password. Use that same iPhone to join the watch's
   setup network and enter those credentials in the watch's portal. If the
   hotspot is not listed while the phone is configuring the watch, enter its
   network name manually.
3. After saving, disconnect the iPhone from the watch's setup network and return to the iPhone's Personal Hotspot settings and remain there
   until the watch connects. Then use the phone normally.

While active, the watch retries when a connection is lost. Battery Saver deliberately
disconnects while idle and does not attempt to keep a hotspot available. It cannot guarantee that an iPhone
will make its hotspot available again automatically; reopening Personal Hotspot
may be needed. Screen lock alone is not a promise of failure or success. Test
your actual iPhone/iOS combination. A carrier plan that supports tethering is
required. [Apple's hotspot troubleshooting](https://support.apple.com/en-us/119837).

## Controls and operation

| Input | Action |
|---|---|
| Tap a dark screen | Wake display; resume Wi-Fi and mining if paused by Saver |
| Swipe left/right | Navigate screens |
| BOOT short press | Wake / cycle screens; in charging-only mode, wake without starting the watch |
| BOOT hold 5 seconds | Block-found demo (does not increment the real block count) |
| PWR short press while dark/dim | Wake display |
| PWR short press while lit | Start the watch from charging-only mode; otherwise sleep the display |
| PWR hold 3 seconds | Save and power off on battery; return to charging-only mode on USB |
| Hold BOOT + PWR together for 5 seconds, then release both | Restart into phone setup with the QR code; preserve saved settings and statistics |

For the setup shortcut, press both buttons together while the watch is running
(including a sleeping display or charging screen). Hold until **Release both
buttons to open setup** appears, then release. Releasing either button before
five seconds cancels the shortcut; neither button performs its ordinary action.
The setup restart bypasses the charging screen even when USB is connected.
This software shortcut cannot recover a completely frozen application; see
**Flash and recovery** above for the independent USB download path.

The screen dims after 10 seconds and turns off after 15 seconds in normal use.
Setup and celebration screens have their own display rules. A dark screen does
not itself pause mining; Saver uses the activity window described above. Offline, connecting and cooling states do not display a stale
hashrate as usable mining. The RTC keeps the clock available without internet.

Active profile on both battery and USB: 80 MHz, one hardware worker, and a 16 ms
gap between batches. Battery Saver stops mining and Wi-Fi when its activity
window expires on battery. They stay off until physical input, turning Saver off,
or attaching USB restores normal operation. Unplugging grants a fresh three-minute
window. Mining always waits for a new connection and authorized pool job after
an idle pause. Thermal restrictions always take priority; these settings are not
a guarantee of a particular hashrate or battery runtime.

## SD card setup

An optional FAT32 microSD card can provide `config.json`. Use
[examples/config.example.json](../examples/config.example.json) and replace its
placeholders. Remove the card after setup because it contains Wi-Fi credentials.
The SD and internal JSON formats retain different key names for compatibility;
both share validation and migration code.

## Development

- [Architecture and ownership](ARCHITECTURE.md)
- [Contributing and tests](../CONTRIBUTING.md)
- [Hardware acceptance tests](HARDWARE_TESTS.md)
- [Change notes](../CHANGELOG.md)
- [Dependency and media notices](../THIRD_PARTY_NOTICES.md)

SOLOMINER firmware and supplied artwork use GPL-3.0-only. Inherited MIT,
third-party, and font notices are preserved. See [LICENSE](../LICENSE),
[licensing scope](../LICENSING.md), and [third-party notices](../THIRD_PARTY_NOTICES.md).

### Diagnostics

The standard firmware includes cooperative light sleep. The separate
[light-sleep probe](LIGHT_SLEEP_TEST.md) remains a diagnostic tool rather
than an alternative product build. See [diagnostics](DIAGNOSTICS.md) for
sanitized USB logs.

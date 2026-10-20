# v3.0.0 hardware acceptance

A successful build and host tests do not establish physical-device behavior.
Run these checks on the supported Waveshare ESP32-S3-Touch-AMOLED-2.06 before
publishing a release. This checklist supersedes the earlier 4.x development
checklists, whose power and UI behavior changed during development.

Record firmware version, source commit, application SHA-256, board/battery
revision, phone OS/router, test date, and results. Keep passwords and personal
network details out of the public report. Leave untested items marked untested.

## Setup, updates and recovery

| Check | Expected result |
|---|---|
| Cold boot without credentials | Provisioning instructions are accessible; Wi-Fi QR scans and manual instructions work. |
| First phone setup | Network, password, pool, receiving address, timezone and clock format save correctly. No private wallet key is requested. |
| Unavailable network or wrong password | Watch remains responsive, shows connecting guidance, retries, and allows setup recovery. It must not claim to know a password is wrong from an ambiguous timeout. |
| Save a different Wi-Fi network | Connecting status uses the newly saved name, never the old name; settings survive reboot. |
| Save timezone while offline | Selection survives setup reopen and restart; local date/time is correct after synchronization, including daylight-saving rules. |
| Leave setup | Portal cleanup happens once without crash. Exit returns to Clock and preserves saved settings. |
| Update through portal | Correct application image installs; wallet, Wi-Fi, timezone, brightness and mining records persist. Wrong/invalid upload is rejected without replacing working firmware. |
| USB installation | Use the supported board and documented image/offset. Verify boot and setup. Record the artifact actually flashed. |
| Five-second recovery shortcut | Hold BOOT and PWR together, then release both. Restart enters setup QR, preserving settings and counters. Test from normal screens, celebration, charging and Saver sleep. |
| Cancel recovery shortcut | Release before five seconds: no restart, demo, power-off or accidental navigation. Normal controls still work afterward. |
| Fully unresponsive application | Verify the board's independent power/download-mode recovery. The software shortcut is not a guarantee against a full application hang. |

## Connectivity and mining

| Check | Expected result |
|---|---|
| Saved network present on battery/USB boot | Connect, authorize with the pool, then start mining. |
| Wi-Fi missing for several minutes | Retries back off; physical wake requests a prompt retry without interrupting an association already in progress. |
| Restore hotspot with display automatically asleep | Eligible connection wakes the display, with cooldown respected. No stale hashrate appears before fresh work. |
| Manual display-off or Saver idle | Network activity does not override intentional display-off or revive an expired Saver window. |
| Pool unavailable or login rejected | Appropriate pool status; no positive mining status without fresh authorized work. Recovery does not require reboot. |
| Internet loss and restoration | No UI freeze, stale-positive hashrate or leaked reconnection resources. |
| USB plug/unplug | Same active mining profile; no boost mode. Power changes do not corrupt mining records or cause a display-driven network restart. |

## Battery and display

| Check | Expected result |
|---|---|
| Saver On, on battery | After three minutes without physical input, mining/Wi-Fi stop and cooperative light sleep starts. Navigation resets to Clock without lighting the screen. |
| Wake from Saver | Touch or button resumes active operation on Clock; Wi-Fi reconnects when available. No periodic hotspot keepalive occurs while paused. |
| Initial successful connection | One eligible connection can extend the active window once. Repeated reconnects cannot keep extending it. |
| Saver Off and USB | Continuous operation subject to thermal/low-battery protection. Ordinary screen timeout does not stop mining. |
| Battery percentage at low/middle/high charge | Readout settles without invented counts or large unexplained jumps. Record actual readings and timing, especially plug/unplug transitions. |
| USB startup before valid percentage | “Reading battery level...” and moving orange arc; valid percentage replaces the message and fills the ring to the measured level. |
| Repeated sleep/wake and dimming | Full frame redraw, correct brightness, no stale text, blank bands or residual pixels. |
| Thermal and low-battery protection | Under controlled safe conditions, protection acts correctly and does not falsely power off on USB. Do not deliberately overheat a lithium battery. |

Measure runtime on a real watch with realistic screen use. Record ambient and
skin-contact temperatures separately from the die sensor. Do not extrapolate
all-day runtime from a short idle test.

## Touch, layout and celebration

- Swipe through Clock, Mining, Device and Settings. Swiping across a setting
  must not activate its button. Test near row edges and between rows.
- Brightness, Battery Saver and Wi-Fi & setup detail screens have consistent
  back navigation. Settings save without an extra Done action; Saver On is green.
- Clock Wi-Fi dots are orange, pool dots blue, mining green. Status/subtitle
  remain aligned and readable with stars behind them. Unknown readings, long
  network names and long uptime fit inside the bezels.
- Splash shows the shared star background, then the astronaut instantly,
  then hands off without a black rectangle or extra blank pause.
- Hold BOOT to start the labeled three-second demo countdown. It must reach
  zero and start; sound plays at the configured 69% level and continues across
  pages for the full song unless muted or closed.
- Verify astronaut/flag placement, orange strike effect, moon layering, and
  beat-synced headings. Text and footer controls remain inside the bezel.
- After the celebration settles, verify the eight-second celebration hold,
  six-second reward hold and final QR hold. Manual swipe or BOOT navigation
  takes control and prevents a surprise automatic page change.
- Check YOUR BLOCK and BLOCK REWARD values, the SEE YOUR BLOCK QR, readable
  numbers, no USD lookup, and the separate dark backgrounds behind volume,
  page dots and close controls. QR must scan from a phone at normal brightness.
- Demo data must remain an example: repeated demos do not increment real counters
  or overwrite a real block record. Test real-block handling through a controlled
  test fixture; do not present a simulated event as a real network block.
- Repeat celebration/start/close and manual navigation to check for heap loss,
  stuck audio, clipping or reboot. Confirm controls work on all three pages.

## Soak and release record

Run extended battery and USB sessions, including repeated setup, Wi-Fi loss,
wake/sleep and demos. Inspect free heap and task stack headroom. For each failure,
record reproduction steps and sanitized diagnostics. Attach a completed report
with the exact release hash before setting hardware acceptance to approved.

Charging controls: from the lit charging screen, tap the lower Power button and confirm the splash then Clock/Setup. From a dark charging screen, the first tap only wakes it and its release must not start it; a second lower-button tap starts. Confirm upper BOOT wakes charging without starting, and a three-second Power hold does not start the watch on release.

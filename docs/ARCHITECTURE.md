# Firmware architecture

## Operating rule

Display off does not mean mining off. Work is allowed only on an authorized pool
session with valid work. With Battery Saver enabled, physical input controls the inactivity window;
radio availability is scheduled by the same network owner. Thermal/low-battery protections take precedence over performance.

## Ownership

| Component | Owner/context | What it controls |
|---|---|---|
| `SoloMinerV2.ino.cpp` | setup/loop | Initializes subsystems; services device policy and LVGL |
| `device/device_power.cpp` | setup/loop; battery decision called by hardware polling task | USB debounce, fixed mining profile, thermal policy, low-battery decision |
| `device/mining_control.*` | power writes; mining/UI read | Atomic performance and thermal limits |
| `network/wifi_connection.cpp` | setup first, then Stratum worker exclusively | Provisioning handoff, Wi-Fi power-save request, runtime reconnect schedule |
| `wManager.cpp` | setup only | Captive portal and explicit reconfiguration; shuts portal down before worker starts |
| `network/connection_policy.h` | Stratum worker | Retry deadlines, authorization state, job freshness |
| `network/authorization.h` | Stratum worker | Matching and validating authorization responses |
| `mining.cpp` / `stratum.cpp` | Stratum + mining workers | Pool exchange, work queues, SHA engine and share submission |
| `ui/mining_status.h` | display reader | Maps connectivity/thermal status into user-facing behavior |
| `drivers/displays/` | existing render/input tasks and LVGL/bus locks | Screen rendering, display sleep, buttons, touch, PMU hardware calls |
| `settings/` | startup loading; existing deferred UI saving | Defaults, normalized choices, shared JSON decoding and migration |

Runtime Wi-Fi operations belong to one worker, which applies the fixed
MIN_MODEM sleep setting. The device controller publishes the inactivity decision.
Startup is a sequential exception: provisioning may use WiFiManager or import SD
credentials before the worker exists. Explicit setup restarts the device rather
than opening a second live network controller.

The display driver still contains board-specific PMU, display and input handling.
Hardware access retains the existing I2C/display locks. It no longer controls
network startup, mining pause or CPU light sleep. Low-battery policy is a device
decision called from the always-running hardware poll, so it works during setup
and when the main display is off. PMU shutdown is performed by that hardware layer.

## Connection lifecycle

1. Service Wi-Fi; retry failed association at 3, 6, 12, 24 then 30-second intervals.
2. After association, allow two seconds for stabilization.
3. Open the pool socket, subscribe and send authorization with a tracked ID.
4. Until explicit successful authorization, retain only the latest early job;
   never hash or submit it. Authorization rejection is visible; no reply after
   15 seconds closes the session for a bounded-rate retry.
5. Start work after authorization. Failed keepalive writes or three minutes
   without new work close the session. An intentional thermal pause does not
   count as failed connectivity. Runtime reconnect attempts have no screen-side
   or activity-timeout interaction.

Pool writes and reads remain in the existing Stratum task; DNS/socket operations
can still block that task for their library timeouts. The display runs separately.
No additional task or generic event framework was introduced.

## Shared-state rules

Power limits and pool phase indicators use atomics. Work queues retain their
existing mutex. LVGL and board buses retain their locks. Other inherited mining
statistics have not undergone a complete concurrency redesign; this change is
not a whole-program race-free certification. New features must not add networking
to a display callback or block inside a mining callback.

## Settings migration

`storage.h` contains defaults and legacy key names. `settings/config_codec.h`
maps internal/SD formats into the same `TSettings` and applies one validation
function. An explicit old timezone offset migrates to a POSIX fixed-offset zone.
Unknown/deleted settings such as `ultraSaver` are ignored. Current settings are
written without the old option. Wi-Fi remains in the ESP32 Wi-Fi storage; routine
application updates preserve existing partition offsets.

## Build and extension points

Start with `events.h` for small non-blocking callbacks. Queue expensive work to an
owner task. `src/version.h` controls version labeling. `platformio.ini` pins
dependencies. `pre_patch_gfx_lib.py` patches only the active environment for
Arduino core 2.x compatibility. `scripts/firmware_artifacts.py` checks segment
sizes and emits checksummed factory/application images; it fails on missing data.

## Watch UI conventions

`ui/mining_status.h` is the shared source for clock/mining status, semantic tone,
and recovery action. Wi-Fi loss takes precedence over old pool state. UI Retry
only requests the network owner's existing reconnect path; it never starts its
own network operation. A rejected login offers setup through confirmation.

`ui/page_controls.h` shares touch and drawing rectangles. Settings has three
100-pixel rows; setup actions use the common bottom action rectangle. Celebration
sound and close actions have separate 72-pixel footer targets, flanking the dots.
The board driver contains small page text, action, message and QR builders, plus
retained normal-page widgets. Setup uses explicit QR/manual/failure/connecting
views. `ui/setup_code.h` escapes the actual provisioning SSID/password for QR
independently of font sanitization. The actual AP name is set before portal startup.

Demo countdown and celebration never write the miner's saved block fields or
counter. The three-second demo countdown is cancellable; historical block samples
and their explorer QR exist only in its render branch. Real finds interrupt a demo
and bypass the countdown. The celebration holds eight seconds after landing, then shows the reward for six seconds and holds the QR page. Swipes or BOOT take manual control. Playback
protects the opening and active song from ordinary idle sleep, then normal sleep
resumes; sleeping preserves the receipt. Saved real records reopen silently. The unused Initializing renderer,
secondary brand wait and classic flashing block path have been removed.

## Diagnostics ownership

`diagnostics/diagnostics.cpp` owns its rolling SPIFFS files, bounded event queue,
minute samples, and read-only USB export. Existing battery reads and mining
monitor results publish atomics; the recorder reads page/display state under the
existing LVGL lock and never draws. Shutdown requests a bounded best-effort flush.
No networking operations are added to display callbacks. Legacy serial output is
compile-time opt-in through `seriallog.h`. `scripts/download_diagnostics.py`
verifies the transfer and creates a redacted-by-construction diagnostic package.
See DIAGNOSTICS.md for schema, retention, limitations and on-device acceptance.

## Battery Saver

Physical input publishes timestamps through `device::noteUserActivity()`.
Programmatic screen wakes do not extend the active window. The first successful
Wi-Fi connection can extend it once via `device::noteWifiConnected()`. Physical
input re-arms the allowance; late events cannot resurrect an expired window.
The saved `idleBatterySaver` / `IdleBatterySaver` boolean remains compatible and
defaults On. On battery, three minutes without input requests idle. Boot and unplug
grant a fresh window; USB bypasses the inactivity timer.

On entering idle the Stratum owner invalidates work, closes the pool socket,
clears authorization and pending submissions, then calls `network::serviceIdle()`.
`IdlePause` has only active/paused states. Wi-Fi is stopped and remains stopped
until the owner returns to foreground service. There is no background join,
dwell, periodic radio wake or pool keepalive during idle. Failed radio shutdown
is retried with bounded backoff; it never starts or reconnects the radio.

Physical input, Saver off or USB resumes the foreground connection path. A
stopped radio must receive a fresh IP event before the pool reconnects. Start
errors use the existing foreground backoff. Mining waits for fresh authorized
work. Workers continue to yield safely while idle. Credentials and initialized
station configuration are retained without recurring NVS writes.

All active mining uses 80 MHz, one hardware worker and a 16 ms batch gap.
MIN_MODEM Wi-Fi sleep is also the same on both power sources. Thermal limits
still override hashing, including critical-temperature pause, and never raise
the CPU to a USB-specific clock. Low-battery shutdown is unchanged.
The display coordinator requests cooperative light sleep only after all six
participants acknowledge safe points. Timer slices are at most two seconds;
physical input resumes the normal path. Diagnostics report phase 0 active and 1 paused;
older phase/event identifiers remain readable for historical logs.


## Block detail pages

Page two shows YOUR BLOCK (recorded height) and BLOCK REWARD (base subsidy in BTC).
Page three says SEE YOUR BLOCK and encodes its hash in a mempool.space explorer QR.
Both pages are local, including saved records. No exchange-rate service, price
cache, HTTPS request or special price-worker sleep gate is present.

## Hash-rate freshness and wake

The Monitor owns rate samples, updating the average once per sampling interval. A new Wi-Fi generation or unusable interval discards the old average and first partial interval. Readers reject stale samples. Display wake defers brightness restoration until the current page has been redrawn and flushed under LVGL ownership, preventing old retained pixels from appearing.

## Reliability and shutdown

Lower-button taps sleep/wake the display, or start the watch from an awake
charging screen. A three-second hold checkpoints settings/statistics and powers
off (USB returns to charging-only mode). The top button can wake the charging
screen without starting the watch. Settings saves snapshot the UI under its lock, then
write outside that lock, acknowledging only the captured revision. Failed writes
remain pending. NVS statistics are one checksummed blob; legacy migration requires
a complete valid CRC record. In-memory counter snapshots and NVS writes use
separate mutexes so flash operations do not hold the counter lock.

The render owner handles battery notices; the power task still owns critical
cutoff. Notices never request a wake. RTC registers are checked before UTC
conversion, including the oscillator-stop flag and calendar validity.

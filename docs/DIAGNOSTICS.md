# Stored watch diagnostics and USB export

This firmware records automatically. No extra watch menu or phone
app is required. Use the watch unplugged, then connect USB after the test.
Installing this firmware is required before any history can be collected; it
cannot recover activity from older firmware. Use the application update image
for the correct brand with the project's normal update process. Do not erase
flash if you want to retain settings or diagnostic history.

## Download on your Mac

1. Close PlatformIO/Arduino serial monitors. Connect a USB data cable and wait
   about ten seconds. The normal charging screen is fine; mining need not start.
2. Double-click `scripts/Download watch diagnostics.command` from the extracted
   helper folder. It uses Python 3 (no extra Python packages).
3. If more than one USB serial port is present, select the watch.
4. The helper saves `SOLOMINER-diagnostics-<date-time>.zip` in Downloads.
5. Attach that ZIP to the conversation. It contains `watch-diagnostics.jsonl`,
   `summary.md`, `transfer.json`, and the field/units dictionary `schema.json`. Export never erases logs.

Python 3 is required. If macOS does not allow launching the downloaded command
file, run `python3 scripts/download_diagnostics.py` from the extracted folder in
Terminal. `--list` lists ports; `--port /dev/cu.usbmodem...` selects one. The
script supports macOS/Linux USB serial; Windows transport is not included.
A previously downloaded JSONL file can be summarized using `--input <path>`.
The helper does not flash firmware or deliberately toggle reset lines. If the
board/USB driver restarts on attachment, stored records survive and the new boot
is a separate session. Physical USB behavior still requires device testing.

## What is captured

- Boot session ID, firmware version, compilation timestamp, brand, ELF identity
  and ESP-IDF reset reason. Timestamps use 64-bit elapsed milliseconds per boot.
  State/sample rows also contain UTC from the system clock; values before 2024
  are unsynchronized and should not be treated as real dates.
- Raw battery millivolts, raw gauge percentage, displayed percentage, gauge
  source (0 hardware estimate, 1 voltage fallback), charge status, and age of
  the last raw reading. Unknown values are -1. Existing cached PMU reads are
  reused; diagnostics adds no extra battery ADC reads.
- USB state, screen state (0 asleep, 1 dim, 2 awake), actual brightness, page,
  demo state, audio playing and audio hardware initialized.
- Wi-Fi association, signal strength (dBm; -127 disconnected), pool phase,
  reconnect attempts, CPU MHz, configured effective mining gap, thermal tier,
  chip temperature in tenths C (-999 not sampled on that row), free/minimum heap.
- Accepted/rejected submission totals since boot and approximate mining rate.
  Hashrate uses the existing monitor counters; it is an estimate, not a new
  independent hardware measurement. `kh_sum` integrates those estimates.
- Cumulative screen-on, associated-Wi-Fi and mining-phase time from observed
  state. These are sampled accounting, not exact task execution times.
- Timestamped demo start/cancel/end, audio initialization/start/stop, Wi-Fi
  retry, real block, requested shutdown, hashing/submission/parse errors and failed settings-save events. No real block identity,
  wallet, pool hostname, SSID, password or raw protocol traffic is stored.

Pages: 0 clock, 1 mining, 2 Device, 3 Settings, 10 charging, 20 setup QR,
21 manual setup, 22 setup failed, 23 connecting, 30 landing, 31 celebration,
32 receipt. Demo state: 0 off, 1 countdown, 2 celebration. Pool phases follow
`src/network/pool_status.h`; thermal tiers are 0 normal through 3 paused.

## Collection and retention

A low-priority task checks state approximately once per second. It emits a
state record only on a relevant change, plus a periodic sample every minute.
Temperature is read only for periodic samples; battery values come from the
existing cache. Discrete events use a 32-entry nonblocking queue and overflow
is counted. No network uploads, extra Wi-Fi scans or new SD-card dependency.

Records buffer in 4 KiB RAM and flush every five minutes, when the buffer fills,
on USB attachment, before export, and best-effort before PMU shutdown. Two
approximately 192 KiB SPIFFS segments retain roughly 384 KiB of rolling history.
The oldest segment is removed at rollover. Retention is by bytes, not guaranteed
hours: event-heavy runs retain less time. Config files and partitions are not
changed. At least 256 KiB filesystem headroom is required before writing.
Storage failures and queue drops are reported in the transfer footer; the
logger never formats storage to recover. If storage is unavailable, durable
recording is unavailable; the device continues operating.

An abrupt crash/power loss may lose up to five minutes of buffered records or
leave an incomplete line. The helper flags malformed records and retains the
original bytes. Rotation/power interruption can also leave only the previous
segment. The logger does not promise transactional capture of every event or a
crash stack trace. Coredump extraction remains a separate developer procedure;
raw memory dumps are intentionally excluded because they may contain secrets.

USB export is read-only, uses hex-framed chunks and a full-transfer CRC-32, and
is bounded by a timeout. The recorder pauses its own file updates while copying
both files. Other watch tasks continue; a long export may overflow the event
queue (reported). Reconnect and retry after an interrupted transfer.

## Interpreting the summary

The helper separates boots, orders events by elapsed time, counts observed
Wi-Fi disconnects, flags large displayed-percentage changes, and groups
battery-only intervals by CPU/gap, screen, Wi-Fi, audio initialization, thermal
state and gauge source. It excludes gaps over 96 seconds, USB intervals and
stale/invalid gauge data from battery trend calculations; trends require at
least 30 minutes of usable observations per group. Groups can combine multiple
intervals. These observations are not randomized experiments and cannot prove
that a particular component caused a drain.

There is no measured mA/mWh value. Battery percentage/voltage are estimates and
can change with load, temperature and gauge correction. Compare several-hour
matched runs, then full runtime tests to normal shutdown. An external battery-
side power profiler is needed for precise power. The logging overhead has not
yet been measured; it should be compared with a recorder-disabled build.

## Console consolidation

Regular firmware sets `SOLOMINER_VERBOSE_LOGS=0`. Legacy Serial print calls are
routed through macros that compile out both output and argument evaluation;
WiFiManager console debug is disabled. The existing repeated zero-hash messages,
share hex dumps and memory chatter are covered by structured diagnostics.
Essential SDK/panic output and existing persistent lifetime mining statistics
are retained. The PlatformIO `log2file` monitor filter only writes on the Mac;
it is not an on-watch battery drain and remains available for developers.

A developer can enable legacy output with `-D SOLOMINER_VERBOSE_LOGS=1`.
That output may include private network/configuration information and is not
part of the diagnostic ZIP. Keep ordinary test builds quiet. USB serial is
re-enabled after VBUS attachment so logs can be downloaded after a battery boot,
and ended again after VBUS removal (detection follows the existing PMU cadence).

## Required physical acceptance

- Application update preserves settings; both brands start and mine normally.
- Battery boot: record at least 15 minutes, attach USB, download successfully
  without requiring a mining start, then detach and confirm continued logging.
- Reboot/power cycle and download again: previous persisted sessions remain.
- Cancel a demo before playback, play/end a demo, reconnect hotspot, change
  brightness, and sleep/wake. Confirm events/summary match observed actions.
- Run beyond the storage cap and verify rollover and available configuration
  space. Interrupt a download and retry; original logs remain intact.
- Check orderly shutdown captures its event; abrupt power loss may lose the
  documented tail. Do not intentionally run below normal battery protection.
- Measure runtime/current with and without recording. This feature is a test
  instrument, not a proven battery-saving change.

Both firmware builds and host tests are necessary but do not replace these
on-device checks. Record physical acceptance results against the exact firmware hash.

## Battery Saver diagnostics

Current `saver_phase` is 0 for foreground and 1 for intentional radio-off idle.
Idle lasts until physical input, Saver Off or USB resumes normal operation.
Expect one `saver_pause` / `saver_resume` pair per pause, and no `saver_wifi_check`,
`saver_wifi_ok` or `saver_wifi_failed` events. `saver_radio_error` can report a
failed stop/start; stop failures back off while continuing to request radio off.
The downloader retains legacy event meanings for earlier logs.

CPU and base duty gap are 80 MHz / 16 ms on both power sources; thermal overrides
remain visible. Verify no hashes accumulate while paused. Physical current,
hotspot rediscovery and battery runtime still need on-watch measurement.

### Legacy records

The decoder retains support for earlier diagnostic records. Phase 2 means a
background join, 3 a connected dwell, 4 connected idle, and 5 reconnecting idle
in those records. The current production policy uses only phases 0 and 1.
Legacy `saver_wifi_check`, `saver_wifi_ok`, and `saver_wifi_failed` events remain
readable but are not expected from the current idle-off policy.

`wifi_disconnected` records non-local Wi-Fi failure callbacks. `wifi_reason` is
the last driver disconnect-reason code, not a diagnosis of phone or router
behavior. Interpret it with the event history rather than isolated samples.

#!/usr/bin/env python3
"""Download SOLOMINER diagnostics over USB, or summarize an existing JSONL file.
Standard library only. USB transport supports macOS/Linux, without resetting or
flashing the device. Logs are never deleted by this helper.
"""
import argparse
import datetime as dt
import glob
import json
import os
from pathlib import Path
import select
import sys
import time
import zipfile
import zlib

MAX_BYTES = 512 * 1024
SCHEMA = {
    'version': 1,
    'timestamps': {'ms': 'Elapsed milliseconds since boot, ordered within b session ID',
                   'utc': 'System UTC Unix seconds; below 1704067200 means not synchronized',
                   'battery_age_ms': 'Age of cached raw battery reading'},
    'battery': {'mv': 'millivolts', 'raw': 'hardware percentage estimate',
                'pct': 'smoothed/displayed percentage', 'src': {0: 'gauge', 1: 'voltage fallback'},
                'chg': 'raw XPowers AXP2101 charger-state enum; not current in mA'},
    'screen': {0: 'asleep', 1: 'dim', 2: 'awake'},
    'page': {0: 'clock', 1: 'mining', 2: 'Device', 3: 'Settings', 10: 'charging',
             20: 'setup QR', 21: 'manual setup', 22: 'setup failed', 23: 'connecting',
             30: 'landing', 31: 'celebration', 32: 'receipt'},
    'pool': {0: 'idle', 1: 'connecting', 2: 'retrying', 3: 'subscribing',
             4: 'authorizing', 5: 'login rejected', 6: 'waiting for job', 7: 'mining',
             8: 'pool ready; hashing paused'},
    'demo': {0: 'off', 1: 'countdown', 2: 'celebration'},
    'thermal': {0: 'normal', 1: 'warm', 2: 'hot', 3: 'paused'},
    'units': {'bright': 'actual brightness 0..255', 'kh': 'approximate KH/s',
              'kh_sum': 'integrated approximate kilohashes', 'duty': 'mining batch idle gap, ms',
              'saver': 'Battery Saver preference: 1 on, 0 off; bypassed on USB',
              'idle': 'v4.3 intentional inactivity pause: 1 paused, 0 active',
              'saver_phase': '0 active; cycling v4.3/v4.3.2: 1 Wi-Fi off, 2 background join, 3 connected dwell; '
                             'v4.3.1: 4 idle connected, 5 idle reconnecting',
              'wifi_reason': 'Last non-local ESP Wi-Fi disconnect reason code; 0 before any failure; retained after reconnect',
              'c10': 'chip temperature tenths Celsius; -999 unavailable', 'rssi': 'dBm; -127 disconnected',
              'on_ms': 'cumulative observed screen-on ms', 'linked_ms': 'cumulative Wi-Fi-associated ms',
              'mining_ms': 'cumulative associated mining-phase ms without thermal pause',
              'heap': 'free heap bytes', 'min_heap': 'minimum free heap bytes since boot'},
    'flags': {'usb': 'VBUS present', 'wifi': 'Wi-Fi associated', 'hw': 'audio hardware initialized',
              'audio': 'audio playback task active'},
    'counters': 'retry, accepted, rejected, drop and storage_errors are cumulative since boot',
    'limitations': 'Unknown values are -1 unless specified. No measured current/energy. '
                   'State transitions sampled near 1 second. No credentials or memory dumps. '
                   'Rolling files may start mid-session; abrupt loss may omit last 5 minutes.'
}

class Transfer:
    def __init__(self):
        self.data = bytearray()
        self.started = False
        self.complete = False
        self.metadata = {}

    def feed(self, line):
        line = line.strip()
        if line == b'SM_DIAG_BEGIN 1':
            if self.started:
                raise ValueError('Watch restarted the transfer. Retry the download.')
            self.started = True
        elif self.started and line.startswith(b'SM_DIAG_DATA '):
            block = bytes.fromhex(line[13:].decode('ascii'))
            if not block or len(block) > 128 or len(self.data) + len(block) > MAX_BYTES:
                raise ValueError('Invalid diagnostic data size.')
            self.data.extend(block)
        elif self.started and line.startswith(b'SM_DIAG_END '):
            fields = line.split()
            if len(fields) != 5:
                raise ValueError('Invalid transfer footer.')
            size, crc, errors, dropped = int(fields[1]), int(fields[2], 16), int(fields[3]), int(fields[4])
            if len(self.data) != size or zlib.crc32(self.data) != crc:
                raise ValueError('Transfer checksum mismatch. Stored logs are safe; retry the download.')
            self.complete = True
            self.metadata = {'bytes': size, 'crc32': '%08x' % crc,
                             'storage_errors_this_boot': errors, 'dropped_events_this_boot': dropped}
        # Ignore unrelated boot/SDK output; it cannot silently corrupt verified data.


def ports():
    patterns = ['/dev/cu.usbmodem*', '/dev/cu.usbserial*', '/dev/ttyACM*', '/dev/ttyUSB*']
    return sorted(set(path for pattern in patterns for path in glob.glob(pattern)))


def download(port, timeout=180):
    import termios
    import tty
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    previous = termios.tcgetattr(fd)
    previous[2] &= ~termios.HUPCL
    try:
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[4] = attrs[5] = termios.B115200
        attrs[2] |= termios.CLOCAL | termios.CREAD
        attrs[2] &= ~termios.HUPCL  # do not request a reset/hangup at close
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIFLUSH)
        transfer = Transfer()
        pending = bytearray()
        deadline = time.monotonic() + timeout
        retry = 0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if not transfer.started and now >= retry:
                os.write(fd, b'SM_DIAG_EXPORT\n')
                retry = now + 5
            if not select.select([fd], [], [], 0.5)[0]:
                continue
            chunk = os.read(fd, 4096)
            if not chunk:
                raise OSError('USB connection closed. Reconnect and retry.')
            pending.extend(chunk)
            while b'\n' in pending:
                line, _, pending = pending.partition(b'\n')
                transfer.feed(line)
                if transfer.complete:
                    return bytes(transfer.data), transfer.metadata
            if len(pending) > 8192:
                raise ValueError('Unexpected USB output. Close other serial-monitor applications.')
        raise TimeoutError('No complete diagnostics received. Confirm the diagnostic firmware is installed, '
                           'use a data-capable USB cable, and close other serial monitors.')
    finally:
        try:
            termios.tcsetattr(fd, termios.TCSANOW, previous)
        except OSError:
            pass
        os.close(fd)


def parse(data):
    sessions = {}
    bad = 0
    for line in data.splitlines():
        if not line.strip():
            continue
        try:
            row = json.loads(line)
            if not isinstance(row, dict) or row.get('t') not in ('boot', 's', 'state', 'event'):
                raise ValueError('Unknown record')
            boot, ms = row['b'], row['ms']
            if type(boot) is not int or type(ms) is not int or ms < 0:
                raise ValueError('Invalid timestamp')
            sessions.setdefault(boot, []).append(row)
        except (ValueError, KeyError, TypeError):
            bad += 1
    # Event publishers may enqueue just before a periodic sample; restore time order.
    for rows in sessions.values():
        rows.sort(key=lambda r: r['ms'])
    return sessions, bad


def summarize(data, metadata):
    sessions, bad = parse(data)
    lines = ['# SOLOMINER diagnostic summary', '',
             'Battery trends are gauge estimates, not measured current or energy. '
             'No passwords, wallet addresses, SSIDs or pool hostnames are collected.', '',
             'Minute samples plus state changes; rolling retention may omit the start of a session. '
             'Abrupt shutdown may lose up to five minutes of buffered history. '
             'USB attachment changes power behavior, so battery comparisons exclude USB periods.', '',
             'Transfer: %d bytes. Storage errors this boot: %s. Dropped events this boot: %s. '
             'Unparseable/incomplete rows: %d.' % (len(data), metadata.get('storage_errors_this_boot', 'unknown'),
             metadata.get('dropped_events_this_boot', 'unknown'), bad)]
    for boot, rows in sessions.items():
        headers = [r for r in rows if r['t'] == 'boot']
        samples = [r for r in rows if r['t'] in ('s', 'state')]
        lines += ['', '## Session %08x' % boot]
        if headers:
            h = headers[-1]
            lines += ['Firmware %s; %s; build %s; reset reason %s.' %
                      (h.get('fw'), h.get('brand'), h.get('build'), h.get('reset')),
                      'Firmware ELF identity: `%s`.' % h.get('elf', 'unavailable')]
        else:
            lines += ['Boot record not retained; firmware/reset metadata unavailable for this session.']
        if not samples:
            lines += ['No state samples retained.']
            continue
        duration = (samples[-1]['ms']-samples[0]['ms'])/3600000
        lines += ['Observed span: %.2f hours; %d state/sample records.' % (duration, len(samples))]
        sleep_samples = [r for r in samples if 'sleep_count' in r]
        if sleep_samples:
            last = sleep_samples[-1]
            lines += ['Light sleep since boot (counters may wrap after long runs): %s entries; %.1f minutes asleep; '
                      '%s failures. Last wake cause: %s; last error: %s.' %
                      (last.get('sleep_count'), last.get('sleep_ms', 0)/60000,
                       last.get('sleep_failures'), last.get('sleep_wake'), last.get('sleep_error'))]
        # Group short adjacent intervals by actual previous state. Exclude gaps,
        # USB transitions, and gauge-source changes from trend calculations.
        groups = {}
        corrections = []
        disconnects = 0
        for a, b in zip(samples, samples[1:]):
            minutes = (b['ms']-a['ms'])/60000
            # Cycling firmware intentionally stops Wi-Fi during idle (v4.3/v4.3.2).
            # The v4.3.1 connected-idle experiment (4/5) must still count losses.
            radio_cycle_idle = any(r.get('idle', 0) == 1 and r.get('saver_phase') not in (4, 5) for r in (a, b))
            if a.get('wifi') == 1 and b.get('wifi') == 0 and not radio_cycle_idle:
                disconnects += 1
            if a.get('pct', -1) >= 0 and b.get('pct', -1) >= 0 and abs(b['pct']-a['pct']) >= 5:
                corrections.append(b['ms']/60000)
            if not 0 < minutes <= 1.6 or a.get('usb') != 0 or b.get('usb') != 0:
                continue
            key = (a.get('mhz'), a.get('duty'), a.get('screen'), a.get('wifi'),
                   a.get('hw'), a.get('thermal'), a.get('src'), a.get('saver'), a.get('idle'))
            g = groups.setdefault(key, {'minutes': 0, 'valid_minutes': 0, 'drop': 0, 'kh': 0})
            g['minutes'] += minutes
            g['kh'] += a.get('kh', 0)*minutes
            if (a.get('src') == b.get('src') and min(a.get('pct', -1), b.get('pct', -1)) >= 0
                    and max(a.get('battery_age_ms', 0), b.get('battery_age_ms', 0)) <= 15000):
                g['drop'] += a['pct']-b['pct']
                g['valid_minutes'] += minutes
        lines += ['Observed Wi-Fi disconnects outside intentional radio-off cycles: %d. Large displayed gauge steps: %d (not proof of sudden power drain).' % (disconnects, len(corrections)), '',
                  '| Battery-only condition | Minutes | Approx. KH/s | Displayed percentage-point change/hour |',
                  '|---|---:|---:|---:|']
        for key, g in sorted(groups.items(), key=lambda item: repr(item[0])):
            cpu, duty, screen, wifi, audio, thermal, src, saver, idle = key
            # Short segments still show duration, but do not pretend to measure drain.
            trend = 'Too short' if g['valid_minutes'] < 30 else '%.2f' % (g['drop']*60/g['valid_minutes'])
            label = '%s MHz; gap %s ms; screen %s; Wi-Fi %s; audio initialized %s; thermal %s; gauge source %s; saver %s; idle %s' % key
            lines += ['| %s | %.1f | %.1f | %s |' % (label, g['minutes'], g['kh']/g['minutes'], trend)]
        lines += ['', 'Audio initialized=1 means hardware was brought up, not that measured current increased. '
                  'Saver: 1 on, 0 off, None unavailable in older logs. Screen: 0 asleep, 1 dim, 2 awake. Gauge source: 0 hardware estimate, 1 voltage fallback. '
                  'Groups pool non-contiguous intervals; compare repeated matched runs before concluding causation.']
        events = [r for r in rows if r['t'] == 'event']
        lines += ['', '### Event timeline', '']
        for r in events:
            lines.append('- %.2f min: %s' % (r['ms']/60000, r.get('event', 'unknown')))
        previous_reason = None
        for r in samples:
            reason = r.get('wifi_reason')
            if reason not in (None, 0, previous_reason):
                lines.append('- %.2f min: last Wi-Fi disconnect reason code %s (watch-side observation; not the iPhone internal cause).' % (r['ms']/60000, reason))
            previous_reason = reason
        if not events:
            lines += ['No discrete events retained.']
        # Report whether the retained audio record changed; do not imply hardware sleep.
        if any(r.get('event') == 'audio_ready' for r in events):
            lines += ['', 'Audio was initialized in this session. Compare battery-only groups above '
                      'before/after initialization, keeping mining, screen, signal and thermal conditions comparable.']
    lines += ['', 'Reset codes follow ESP-IDF esp_reset_reason_t (1 power-on, 3 software, 4 panic, '
              '5 interrupt watchdog, 6 task watchdog, 7 other watchdog, 8 deep sleep, 9 brownout). '
              'A reset reason is not a crash backtrace; memory dumps are not exported.', '']
    return '\n'.join(lines)


def package(data, metadata, output):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('watch-diagnostics.jsonl', data)
        archive.writestr('summary.md', summarize(data, metadata))
        archive.writestr('transfer.json', json.dumps(metadata, indent=2))
        archive.writestr('schema.json', json.dumps(SCHEMA, indent=2))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Explicit serial port; otherwise discover it')
    parser.add_argument('--input', type=Path, help='Summarize an already downloaded JSONL file')
    parser.add_argument('--output', type=Path, help='Output ZIP path (must not already exist)')
    parser.add_argument('--list', action='store_true', help='List candidate USB serial ports')
    args = parser.parse_args()
    if args.list:
        print('\n'.join(ports()) or 'No watch USB serial ports found.')
        return
    if args.input:
        data = args.input.read_bytes()
        metadata = {'source': 'existing file', 'bytes': len(data)}
    else:
        port = args.port
        if not port:
            choices = ports()
            if not choices:
                raise RuntimeError('No watch found. Plug it in with a USB data cable and wait 10 seconds. '
                                   'The diagnostic firmware must be installed first.')
            if len(choices) > 1:
                print('Choose the watch USB port:')
                for i, candidate in enumerate(choices, 1):
                    print('  %d. %s' % (i, candidate))
                selected = int(input('Number: '))
                if not 1 <= selected <= len(choices):
                    raise ValueError('Invalid port selection')
                port = choices[selected-1]
            else:
                port = choices[0]
        print('Downloading stored diagnostics from %s. Logs will remain on the watch.' % port)
        data, metadata = download(port)
    if not data.strip():
        raise RuntimeError('The watch returned no stored records. Check storage/firmware and retry after use.')
    stamp = dt.datetime.now().strftime('%Y-%m-%d_%H-%M-%S-%f')
    output = args.output or Path.home()/'Downloads'/('SOLOMINER-diagnostics-'+stamp+'.zip')
    print('Saved: %s\nAttach this ZIP to the conversation.' % package(data, metadata, output))

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print('Download failed: %s' % error, file=sys.stderr)
        sys.exit(1)

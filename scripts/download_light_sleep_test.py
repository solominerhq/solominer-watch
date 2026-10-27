#!/usr/bin/env python3
"""Export the isolated sleep probe using the existing verified USB transport."""
import argparse
import datetime as dt
import json
from pathlib import Path
import sys
import zipfile
from download_diagnostics import download, ports


def summarize(data):
    runs = {}
    boots = []
    bad = 0
    for line in data.splitlines():
        if not line.strip():
            continue
        try:
            r = json.loads(line)
            if (r.get('t') != 'probe' or r.get('schema') != 1
                    or type(r.get('b')) is not int or type(r.get('run')) is not int
                    or type(r.get('ms')) is not int or r['ms'] < 0
                    or not isinstance(r.get('event'), str)):
                raise ValueError('Invalid probe record')
            for k in ('cycle', 'planned_ms', 'slept_ms', 'wake', 'error', 'join_ms', 'rssi', 'pins', 'usb', 'raw', 'mv'):
                if type(r.get(k)) is not int:
                    raise ValueError('Invalid measurement')
            if r['event'] == 'boot':
                boots.append(r)
            else:
                runs.setdefault((r['b'], r['run']), []).append(r)
        except (ValueError, KeyError, TypeError, AttributeError):
            bad += 1
    lines = ['# SOLOMINER light-sleep test', '',
             'This isolated firmware tests sleep/wake and saved Wi-Fi reconnection. '
             'It does not mine or test the normal UI, pool reconnection, audio, or total production battery life.', '',
             'Invalid or torn rows: %d. Raw data is preserved in this ZIP.' % bad,
             'Boots recorded: %d. ESP reset reasons: %s.' % (len(boots), ', '.join(str(b.get('reset')) for b in boots))]
    if not runs:
        lines += ['', 'No test run recorded. Unplug the watch and press BOOT to start.']
    for (boot, run), rows in runs.items():
        rows.sort(key=lambda r: r['ms'])
        enters = [r for r in rows if r['event'] == 'sleep_enter']
        exits = [r for r in rows if r['event'] == 'sleep_exit']
        rejoins = [r for r in rows if r['event'] == 'rejoined']
        cycles = {}
        for r in rows:
            if r['cycle'] >= 0:
                cycles.setdefault(r['cycle'], []).append(r)
        passed = 0
        lines += ['', '## Run %08x / boot %08x' % (run, boot), '',
                  'Last event: **%s**. Elapsed recorded time: %.1f minutes.' % (rows[-1]['event'], (rows[-1]['ms']-rows[0]['ms'])/60000), '',
                  '| Cycle | Planned sleep | Actual sleep | Wake | Wi-Fi rejoin | Result |',
                  '|---|---:|---:|---|---:|---|']
        for cycle, records in sorted(cycles.items()):
            entry = [r for r in records if r['event'] == 'sleep_enter']
            wake = [r for r in records if r['event'] == 'sleep_exit']
            rejoin = [r for r in records if r['event'] == 'rejoined']
            a = wake[-1] if wake else None
            j = rejoin[-1] if rejoin else None
            ok = (len(entry) == len(wake) == len(rejoin) == 1 and a['error'] == 0
                  and a['planned_ms'] == (10000 if cycle == 0 else 60000 if cycle == 1 else 180000)
                  and a['wake'] == 4 and a['planned_ms']-500 <= a['slept_ms'] <= a['planned_ms']+2000
                  and a['usb'] == 0 and j['usb'] == 0)
            if ok:
                passed += 1
            reason = 'timer' if a and a['wake'] == 4 else 'GPIO, mask %s' % a['pins'] if a and a['wake'] == 7 else str(a['wake']) if a else 'missing'
            planned = records[0]['planned_ms']/1000
            lines += ['| %d | %.1f s | %s | %s | %s | %s |' % (
                cycle+1, planned, ('%.3f s' % (a['slept_ms']/1000)) if a else 'missing', reason,
                ('%.2f s' % (j['join_ms']/1000)) if j else 'not completed', 'PASS' if ok else 'Review')]
        complete = (passed == 22 and set(cycles) == set(range(22))
                    and rows[0]['event'] == 'start' and rows[-1]['event'] == 'complete' and bad == 0
                    and sum(r['event'] == 'initial_join' for r in rows) == 1)
        lines += ['', '**%s** — %d/22 timer-sleep-and-reconnect cycles passed.' % ('Sequence complete' if complete else 'Sequence incomplete or needs review', passed),
                  'GPIO mask: 1 BOOT, 2 PWR, 4 touch; pins can be released before sampling.',
                  'A deliberate input wake ends the run and is expected to produce an incomplete sequence.',
                  'Rejoin time ends at Wi-Fi connected; successful rows also require a further five-second connected dwell.',
                  'Gauge endpoints: %s%% / %s mV → %s%% / %s mV. No measured current or runtime claim.' % (rows[0]['raw'], rows[0]['mv'], rows[-1]['raw'], rows[-1]['mv'])]
    return '\n'.join(lines)+'\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port')
    ap.add_argument('--input', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    if args.input:
        data = args.input.read_bytes()
        meta = {'source': 'existing file; transfer not verified here'}
    else:
        port = args.port
        if not port:
            found = ports()
            if not found:
                ap.error('No USB watch found. Plug in, tap PWR to wake, and wait ten seconds.')
            if len(found) == 1:
                port = found[0]
            else:
                for i, value in enumerate(found, 1):
                    print('%d: %s' % (i, value))
                try:
                    choice = int(input('Watch port number: '))
                    if not 1 <= choice <= len(found):
                        raise ValueError()
                    port = found[choice-1]
                except (ValueError, EOFError):
                    ap.error('Invalid port selection; run again with --port.')
        data, meta = download(port)
    result = args.output or Path.home()/'Downloads'/('SOLOMINER-sleep-test-'+dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'.zip')
    result.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(result, 'x', zipfile.ZIP_DEFLATED) as z:
        z.writestr('sleep-test.jsonl', data)
        z.writestr('summary.md', summarize(data))
        z.writestr('transfer.json', json.dumps(meta, indent=2))
    print('Saved:', result)

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, TimeoutError) as error:
        sys.exit('Download failed: '+str(error))

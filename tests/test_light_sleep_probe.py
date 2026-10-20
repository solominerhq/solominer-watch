import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from download_light_sleep_test import summarize


def row(event, cycle=-1, ms=0, **kwargs):
    result = dict(t='probe', schema=1, b=1, run=2, ms=ms, event=event,
                  cycle=cycle, planned_ms=10000 if cycle == 0 else 60000 if cycle == 1 else 180000,
                  slept_ms=10000 if cycle == 0 else 60000 if cycle == 1 else 180000,
                  wake=4, error=0, join_ms=1200, rssi=-50, pins=0, usb=0, raw=80, mv=4000)
    result.update(kwargs)
    return result


def encode(rows):
    return b'\n'.join(json.dumps(r).encode() for r in rows)


def complete():
    rows = [row('start'), row('initial_join', ms=1)]
    for cycle in range(22):
        rows += [row('sleep_enter', cycle, cycle*200000+2),
                 row('sleep_exit', cycle, cycle*200000+180002),
                 row('rejoined', cycle, cycle*200000+181202)]
    rows += [row('complete', ms=22*200000)]
    return rows


class SleepProbeSummary(unittest.TestCase):
    def test_complete_sequence(self):
        summary = summarize(encode(complete()))
        self.assertIn('**Sequence complete**', summary)
        self.assertIn('22/22', summary)

    def test_gpio_wake_is_not_timer_success(self):
        rows = [row('start'), row('sleep_enter', 0, 1),
                row('sleep_exit', 0, 2000, wake=7, pins=4, slept_ms=1999),
                row('user_wake', ms=2001)]
        summary = summarize(encode(rows))
        self.assertIn('GPIO, mask 4', summary)
        self.assertIn('0/22', summary)

    def test_rejection_short_sleep_usb_and_duplicate_fail(self):
        for update in ({'error': 259}, {'slept_ms': 1}, {'usb': 1}):
            rows = complete()
            rows[3].update(update)
            summary = summarize(encode(rows))
            self.assertNotIn('**Sequence complete**', summary)
        rows = complete()
        rows.insert(4, dict(rows[3]))
        self.assertNotIn('**Sequence complete**', summarize(encode(rows)))

    def test_torn_row_and_incomplete_resume(self):
        summary = summarize(encode([row('start'), row('sleep_enter', 0, 1)])+b'\n{"t":')
        self.assertIn('Invalid or torn rows: 1', summary)
        self.assertIn('missing', summary)
        self.assertNotIn('**Sequence complete**', summary)

if __name__ == '__main__':
    unittest.main()

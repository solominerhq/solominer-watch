import importlib.util
import json
import os
from pathlib import Path
import pty
import tempfile
import threading
import unittest
import zipfile
import zlib

spec=importlib.util.spec_from_file_location('download_diagnostics',Path(__file__).resolve().parents[1]/'scripts/download_diagnostics.py')
diag=importlib.util.module_from_spec(spec);spec.loader.exec_module(diag)

def frames(data):
    result=[b'SM_DIAG_BEGIN 1\n']
    result += [b'SM_DIAG_DATA '+data[i:i+128].hex().encode()+b'\n' for i in range(0,len(data),128)]
    result += [('SM_DIAG_END %d %08x 0 0\n' % (len(data),zlib.crc32(data))).encode()]
    return result

def sample(ms,pct,usb=0,source=0):
    return dict(t='s',b=1,ms=ms,pct=pct,usb=usb,src=source,screen=0,mhz=80,duty=16,
                wifi=1,hw=0,thermal=0,kh=125,battery_age_ms=1000)

class DiagnosticsTests(unittest.TestCase):
    def test_verified_transfer_ignores_unrelated_output(self):
        payload=(b'{"t":"s","b":1,"ms":10}\n'*100)
        t=diag.Transfer()
        for line in [b'boot output\n']+frames(payload):t.feed(line)
        self.assertTrue(t.complete);self.assertEqual(bytes(t.data),payload)
    def test_corruption_and_truncation(self):
        stream=frames(b'hello\n');t=diag.Transfer()
        t.feed(stream[0]);t.feed(b'SM_DIAG_DATA 68656c6c6f21\n')
        with self.assertRaises(ValueError):t.feed(stream[-1])
        t=diag.Transfer()
        for line in stream[:-1]:t.feed(line)
        self.assertFalse(t.complete)
    def test_oversize_and_duplicate_start(self):
        t=diag.Transfer();t.feed(b'SM_DIAG_BEGIN 1')
        with self.assertRaises(ValueError):t.feed(b'SM_DIAG_DATA '+b'aa'*129)
        with self.assertRaises(ValueError):t.feed(b'SM_DIAG_BEGIN 1')
    def test_torn_records_and_event_order(self):
        data=b'{torn\n'+json.dumps(dict(t='s',b=1,ms=100)).encode()+b'\n'+json.dumps(dict(t='event',b=1,ms=50,event='demo_start')).encode()
        sessions,bad=diag.parse(data)
        self.assertEqual(bad,1);self.assertEqual([r['ms'] for r in sessions[1]],[50,100])
    def test_summary_excludes_usb_and_preserves_unknowns(self):
        rows=[sample(i*60000,90-i//10) for i in range(31)]
        rows += [sample(31*60000,1,usb=1),sample(32*60000,100,usb=1)]
        report=diag.summarize('\n'.join(map(json.dumps,rows)).encode(),{})
        self.assertIn('| 30.0 | 125.0 | 6.00 |',report)
        self.assertIn('Boot record not retained',report)
        self.assertIn('not measured current',report)
    def test_light_sleep_counters(self):
        row = dict(sample(60000,90),sleep_count=25,sleep_ms=50000,sleep_failures=1,sleep_wake=7,sleep_error=0)
        report=diag.summarize(json.dumps(row).encode(),{})
        self.assertIn('25 entries; 0.8 minutes asleep; 1 failures',report)
        self.assertIn('Last wake cause: 7; last error: 0.',report)

    def test_idle_disconnects_are_separate(self):
        rows=[sample(0,90),dict(sample(1000,90),wifi=0,idle=1),
              dict(sample(2000,90),idle=0),dict(sample(3000,90),wifi=0,idle=0)]
        report=diag.summarize('\n'.join(map(json.dumps,rows)).encode(),{})
        self.assertIn('outside intentional radio-off cycles: 1.',report)
        self.assertIn('idle 1',report)

    def test_connected_idle_disconnect_is_reported(self):
        rows=[dict(sample(0,90),idle=1,saver_phase=4,wifi_reason=0),
              dict(sample(1000,90),wifi=0,idle=1,saver_phase=5,wifi_reason=201),
              dict(sample(2000,90),idle=1,saver_phase=4,wifi_reason=201)]
        report=diag.summarize('\n'.join(map(json.dumps,rows)).encode(),{})
        self.assertIn('outside intentional radio-off cycles: 1.',report)
        self.assertEqual(report.count('disconnect reason code 201'),1)

    def test_restored_radio_cycle_is_not_an_unexpected_disconnect(self):
        rows=[dict(sample(0,90),idle=0,saver_phase=0),
              dict(sample(1000,90),wifi=0,idle=1,saver_phase=1),
              dict(sample(61000,90),wifi=0,idle=1,saver_phase=2),
              dict(sample(62000,90),idle=1,saver_phase=3),
              dict(sample(67000,90),wifi=0,idle=1,saver_phase=1)]
        report=diag.summarize('\n'.join(map(json.dumps,rows)).encode(),{})
        self.assertIn('outside intentional radio-off cycles: 0.',report)

    def test_export_preserves_file_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'diagnostics.zip';data=json.dumps(sample(0,80)).encode()
            diag.package(data,{},path)
            with zipfile.ZipFile(path) as z:
                self.assertEqual(z.read('watch-diagnostics.jsonl'),data)
                self.assertIsNone(z.testzip())
            with self.assertRaises(FileExistsError):diag.package(data,{},path)
    def test_actual_usb_protocol_over_pseudo_terminal(self):
        master,slave=pty.openpty();port=os.ttyname(slave)
        payload=json.dumps(sample(0,80)).encode()+b'\n';errors=[]
        def watch():
            try:
                command=b''
                while b'SM_DIAG_EXPORT\n' not in command:
                    command+=os.read(master,128)
                for line in frames(payload):
                    # Deliberately fragment across serial reads.
                    os.write(master,line[:7]);os.write(master,line[7:])
            except Exception as e:errors.append(e)
        worker=threading.Thread(target=watch,daemon=True);worker.start()
        try:
            data,meta=diag.download(port,timeout=5)
            self.assertEqual(data,payload);self.assertEqual(meta['bytes'],len(payload))
            worker.join(2);self.assertFalse(worker.is_alive());self.assertFalse(errors)
        finally:os.close(master);os.close(slave)

if __name__=='__main__':unittest.main()

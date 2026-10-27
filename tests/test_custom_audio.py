"""Optional audio builds must never depend on or export a private recording."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from celebration_audio import TRACK, generate, validate_mp3
from check_public_audio import check_audio_file


class CustomAudio(unittest.TestCase):
    def test_empty_add_replace_remove_and_public_override(self):
        mono = (ROOT / 'tests/fixtures/audio/tone-mono-16k.mp3').read_bytes()
        stereo = (ROOT / 'tests/fixtures/audio/tone-stereo-16k.mp3').read_bytes()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build = root / '.pio/build/test'
            header = build / 'generated/celebration_track_data.h'
            track = root / TRACK
            self.assertEqual(generate(root, build)['mode'], 'silent')
            silent = header.read_bytes()
            stamp = header.stat().st_mtime_ns
            generate(root, build)
            self.assertEqual(header.stat().st_mtime_ns, stamp)
            track.parent.mkdir(parents=True)
            track.write_bytes(mono)
            self.assertEqual(generate(root, build)['mode'], 'custom-local')
            first = header.read_bytes()
            self.assertNotEqual(first, silent)
            track.write_bytes(stereo)
            self.assertEqual(generate(root, build)['channels'], 2)
            self.assertNotEqual(header.read_bytes(), first)
            self.assertEqual(generate(root, build, public=True)['mode'], 'silent')
            self.assertEqual(header.read_bytes(), silent)
            self.assertEqual(track.read_bytes(), stereo)  # Public build does not delete local music.
            track.unlink()
            self.assertEqual(generate(root, build)['mode'], 'silent')
            self.assertEqual(header.read_bytes(), silent)

    def test_wrong_rate_truncation_and_malformed_input_fail_before_compilation(self):
        mono = (ROOT / 'tests/fixtures/audio/tone-mono-16k.mp3').read_bytes()
        bad_rate = (ROOT / 'tests/fixtures/audio/tone-unsupported-44k.mp3').read_bytes()
        for data in (b'', b'not audio', b'ID3', mono[:-1], mono + b'garbage', bad_rate, b'x' * (2 * 1024 * 1024 + 1)):
            with self.assertRaises(ValueError):
                validate_mp3(data)

    def test_production_track_provider_links_in_both_modes(self):
        fixture = ROOT / 'tests/fixtures/audio/tone-mono-16k.mp3'
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build = root / 'build'
            for supplied in (False, True, False):
                track = root / TRACK
                if supplied:
                    track.parent.mkdir(parents=True, exist_ok=True)
                    track.write_bytes(fixture.read_bytes())
                else:
                    track.unlink(missing_ok=True)
                generate(root, build)
                check = root / 'check.cpp'
                check.write_text('#include "celebration_track.h"\n#include <cassert>\nint main(){'
                                 'assert(celebrationTrackSize() == ' + str(fixture.stat().st_size if supplied else 0) + ');'
                                 'assert(bool(celebrationTrackData()) == ' + ('true' if supplied else 'false') + ');}')
                subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                                '-I'+str(ROOT/'src/drivers/audio'), '-I'+str(build/'generated'),
                                str(check), str(ROOT/'src/drivers/audio/celebration_track.cpp'),
                                '-o', str(root/'check')], check=True)
                subprocess.run([str(root/'check')], check=True)

    def test_export_rejects_legacy_generated_and_unreviewed_audio(self):
        for name in ('src/drivers/audio/blockfound_mp3.c', 'src/drivers/audio/blockfound_pcm.c',
                     'src/celebration_track_data.h', 'private/track.mp3', 'assets/song.mp3',
                     'tests/fixtures/audio/tone-mono-16k.mp3'):
            with self.assertRaises(ValueError):
                check_audio_file(name, b'unreviewed recording')
        for p in (ROOT / 'tests/fixtures/audio').glob('*.mp3'):
            check_audio_file(p.relative_to(ROOT), p.read_bytes())

    def test_import_and_remove_without_ffmpeg(self):
        # Exercise the actual CLI in an isolated project; never touch a developer's track.
        import shutil
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'scripts').mkdir()
            for name in ('celebration_audio.py', 'set_celebration_audio.py'):
                shutil.copy2(ROOT/'scripts'/name, root/'scripts'/name)
            fixture = ROOT/'tests/fixtures/audio/tone-mono-16k.mp3'
            command = [sys.executable, str(root/'scripts/set_celebration_audio.py')]
            subprocess.run(command + [str(fixture), '--ffmpeg', 'not-installed'], check=True, capture_output=True)
            self.assertEqual((root/TRACK).read_bytes(), fixture.read_bytes())
            invalid = root/'invalid.mp3'
            invalid.write_bytes(b'invalid')
            result = subprocess.run(command + [str(invalid), '--ffmpeg', 'not-installed'], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((root/TRACK).read_bytes(), fixture.read_bytes())
            subprocess.run(command + ['--remove'], check=True, capture_output=True)
            self.assertFalse((root/TRACK).exists())

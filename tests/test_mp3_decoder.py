"""Exercise the real vendored decoder with original, redistributable test tones."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Mp3DecoderTests(unittest.TestCase):
    def test_real_decoder_and_malformed_inputs(self):
        with tempfile.TemporaryDirectory(prefix="solominer-mp3-") as folder:
            binary = Path(folder) / "decoder-test"
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-O2",
                "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "src"),
                str(ROOT / "tests/mp3_decoder_tests.cpp"),
                str(ROOT / "src/drivers/audio/mp3_decoder.cpp"),
                "-o", str(binary),
            ], check=True)
            fixtures = ROOT / "tests/fixtures/audio"
            subprocess.run([str(binary)] + [str(fixtures / name) for name in (
                "tone-mono-16k.mp3", "tone-stereo-16k.mp3", "tone-unsupported-44k.mp3"
            )], check=True, timeout=15)

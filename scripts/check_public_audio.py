"""Reject legacy recordings and unreviewed audio from a source export."""
from pathlib import Path
import hashlib

# Only original synthetic decoder fixtures may be committed as audio.
FIXTURES = {
    'tests/fixtures/audio/tone-mono-16k.mp3': 'bcbb78d66335d27461c97c52b0c8d0037ed2ff00dfa633682020429012c1f4b2',
    'tests/fixtures/audio/tone-stereo-16k.mp3': 'a2332a0c1016aa12b3fd35ae3141d04313428103bb273d76fdb3bb855e7d496b',
    'tests/fixtures/audio/tone-unsupported-44k.mp3': '041c6b4b34f7ea93bb62bff9ee3de3504490019dbb908eed284f84828dcb3fc5',
}
LEGACY = {'blockfound_mp3.c', 'blockfound_mp3.h', 'blockfound_pcm.c',
          'blockfound_pcm.h', 'blockfound_audio.c', 'blockfound_audio.h',
          'celebration_track_data.h'}


def check_audio_file(name, data):
    name = Path(name)
    if name.name in LEGACY or 'private' in name.parts or '.pio' in name.parts:
        raise ValueError('Local/generated audio cannot be exported: ' + str(name))
    if name.suffix.lower() in {'.mp3', '.wav', '.flac', '.aac', '.m4a', '.ogg', '.opus', '.pcm'}:
        if FIXTURES.get(name.as_posix()) != hashlib.sha256(data).hexdigest():
            raise ValueError('Unreviewed audio in public source: ' + str(name))

"""Local audio packaging. GPL-3.0-only; recordings retain their own rights."""
from pathlib import Path
import hashlib
import json
import os

TRACK = Path('private/celebration/track.mp3')
MAX_BYTES = 2 * 1024 * 1024


def validate_mp3(data):
    """Check every MPEG-2 Layer III frame, not just a filename or first header.

    The ESP32 output runs at 16 kHz; no resampling happens on the device.
    ID3v2 at the beginning and ID3v1 at the end are accepted.
    Decoder regression tests separately exercise the real compressed payload.
    """
    if not data or len(data) > MAX_BYTES:
        raise ValueError('Track must be nonempty and at most 2 MiB.')
    pos = 0
    if data[:3] == b'ID3':
        if len(data) < 10 or data[3] not in (2, 3, 4) or any(b & 128 for b in data[6:10]):
            raise ValueError('Invalid ID3 header.')
        size = 0
        for b in data[6:10]:
            size = (size << 7) | b
        pos = 10 + size + (10 if data[3] == 4 and data[5] & 16 else 0)
    end = len(data) - 128 if data[-128:-125] == b'TAG' else len(data)
    frames = 0
    channels = None
    while pos < end:
        if pos + 4 > end:
            raise ValueError('Truncated MP3 header.')
        h = int.from_bytes(data[pos:pos + 4], 'big')
        # MPEG-2, Layer III, sample-rate index 2 = 16 kHz.
        if h >> 21 != 0x7ff or (h >> 19) & 3 != 2 or (h >> 17) & 3 != 1 or (h >> 10) & 3 != 2:
            raise ValueError('Expected a complete 16 kHz Layer III MP3. Convert with the import tool.')
        bitrate = (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0)[(h >> 12) & 15]
        if not bitrate:
            raise ValueError('Unsupported MP3 bitrate.')
        channel_count = 1 if (h >> 6) & 3 == 3 else 2
        if channels is not None and channel_count != channels:
            raise ValueError('MP3 channel count must remain constant.')
        channels = channel_count
        size = 72000 * bitrate // 16000 + ((h >> 9) & 1)
        if pos + size > end:
            raise ValueError('Truncated MP3 frame.')
        pos += size
        frames += 1
    if frames < 2 or pos != end:
        raise ValueError('No complete MP3 stream found.')
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'sample_rate': 16000, 'channels': channels, 'frames': frames}


def write_changed(path, content):
    path = Path(path)
    if path.exists() and path.read_bytes() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_bytes(content)
    temporary.replace(path)


def generate(root, build, public=False):
    root, build = Path(root), Path(build)
    track = root / TRACK
    # A public build must never incorporate a workstation's private music.
    if not public and track.exists() and track.stat().st_size > MAX_BYTES:
        raise ValueError('Local recording exceeds the 2 MiB audio limit.')
    data = b'' if public or not track.exists() else track.read_bytes()
    info = validate_mp3(data) if data else {'bytes': 0, 'sha256': None}
    rows = [','.join('0x%02x' % b for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    body = ',\n'.join(rows) if rows else '0'
    header = ('// Generated locally. Never commit or export this file.\n'
              '#pragma once\n#include <stddef.h>\n#include <stdint.h>\n'
              'static const uint8_t trackBytes[] = {\n' + body + '\n};\n'
              'static const size_t trackSize = ' + str(len(data)) + ';\n')
    write_changed(build / 'generated/celebration_track_data.h', header.encode())
    info['mode'] = 'custom-local' if data else 'silent'
    write_changed(build / 'celebration_audio.json', (json.dumps(info, indent=2) + '\n').encode())
    return info


def public_build():
    return os.environ.get('SOLOMINER_PUBLIC_BUILD', '') == '1'

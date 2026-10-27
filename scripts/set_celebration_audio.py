"""Import your own recording, or remove it to return to the silent default."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile
from celebration_audio import TRACK, MAX_BYTES, validate_mp3, write_changed

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('input', type=Path, nargs='?', help='Recording you have permission to use')
    choice.add_argument('--remove', action='store_true', help='Remove the local recording')
    parser.add_argument('--ffmpeg', default='ffmpeg', help='FFmpeg executable for conversion')
    args = parser.parse_args()
    target = ROOT / TRACK
    if args.remove:
        target.unlink(missing_ok=True)
        print('Local recording removed. The next build will be silent.')
        return
    try:
        source = args.input.resolve(strict=True)
        # Preserve already-compatible MP3 bytes exactly, including the private original.
        data = source.read_bytes() if source.stat().st_size <= MAX_BYTES else b''
        try:
            info = validate_mp3(data)
        except ValueError:
            executable = shutil.which(args.ffmpeg)
            if not executable:
                parser.error('This recording needs conversion. Install FFmpeg or pass --ffmpeg /path/to/ffmpeg.')
            with tempfile.TemporaryDirectory(prefix='solominer-audio-') as folder:
                converted = Path(folder) / 'track.mp3'
                subprocess.run([executable, '-nostdin', '-v', 'error', '-i', str(source),
                                '-map', '0:a:0', '-vn', '-ac', '1', '-ar', '16000',
                                '-c:a', 'libmp3lame', '-b:a', '48k', '-map_metadata', '-1',
                                '-id3v2_version', '0', str(converted)], check=True)
                if converted.stat().st_size > MAX_BYTES:
                    parser.error('Converted recording exceeds 2 MiB. Shorten it first.')
                data = converted.read_bytes()
                info = validate_mp3(data)
        write_changed(target, data)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    print('Local track ready: %d bytes, %d Hz. Run pio run -e Waveshare-AMOLED-206.' %
          (info['bytes'], info['sample_rate']))
    print('The recording is ignored by Git and excluded from source exports. Custom firmware contains it.')


if __name__ == '__main__':
    main()

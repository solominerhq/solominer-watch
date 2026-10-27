# Original decoder test tones

These three MP3 files contain only synthesized sine waves generated for SoloMiner
on 2026-10-20. No recording from the celebration song is included.
These fixtures are dedicated to the public domain under CC0 1.0; see
`../../../licenses/minimp3-CC0.txt` for the license text.

Generated with FFmpeg/libmp3lame: 0.5-second sine wave, 40 kbit/s, no Xing or ID3 tags.
`tone-mono-16k.mp3`: 440 Hz, 16 kHz mono.
`tone-stereo-16k.mp3`: 660 Hz, 16 kHz stereo (duplicated signal).
`tone-unsupported-44k.mp3`: 440 Hz, 44.1 kHz mono, intentionally rejected by the
watch's fixed-16-kHz speaker pipeline.

Example command:
```
ffmpeg -f lavfi -i 'sine=frequency=440:sample_rate=16000:duration=0.5' -ac 1 -ar 16000 -c:a libmp3lame -b:a 40k -write_xing 0 -id3v2_version 0 -map_metadata -1 tone-mono-16k.mp3
```

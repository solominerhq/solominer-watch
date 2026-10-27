# Bring your own celebration music

The public firmware contains **no song**. The astronaut, Block Found reveal,
reward page, page tour and QR work silently. There is no speaker control when
no track is installed. No account, API key, or network audio service is needed.

## Add or replace a track

From the project folder, run:

```sh
python scripts/set_celebration_audio.py "/path/to/my-song.mp3"
pio run -e Waveshare-AMOLED-206
```

The importer accepts an existing 16 kHz MP3 unchanged. For other MP3 formats,
WAV, or other audio inputs, install FFmpeg first; the tool converts to mono
16 kHz / 48 kbit/s MP3. Use `--ffmpeg /path/to/ffmpeg` if it is not on your PATH.
The converted track must fit within 2 MiB. Invalid imports leave the previous
track intact. Only use recordings you have permission to use and distribute.

The local file is `private/celebration/track.mp3`, ignored by Git. A pre-build
step validates its frame headers and generates the embedded bytes **inside
`.pio/`**, never inside tracked `src/`. Changing or removing the recording
regenerates that header, including for incremental builds. Do not commit the
generated header, private directory, or firmware containing a private track.
The public export tool excludes them and rejects the old embedded-audio files.

To return to the default silent experience:

```sh
python scripts/set_celebration_audio.py --remove
pio run -e Waveshare-AMOLED-206
```

The import changes build-time audio only. It is not an on-watch file picker or
a phone-portal upload feature. Flash the newly built application as usual.

## Timing and volume

Playback uses the first accepted PCM frame as time zero, so device initialization
does not run the animation ahead of the music. The complete recording plays
unless the user exits. A short or failed recording cannot trap the opening.

Edit `src/ui/block_found_sequence.h` to align another song: `touchdownMs`,
`windupMs`, `impactMs`, `dotsMs`, and the hold durations. The default title
palette alternates white/orange at `beatsPerMinute` (default 90) after `impactMs`.
Change that one tempo value to match another song; there is no automatic beat detection.
Edit `AUD_VOLUME` in `src/drivers/audio/audio.cpp` to change the codec volume
(0–100, default 69). Avoid clipped input, and check the result on the speaker.

Mute preserves the song clock; exit stops playback. A fresh run resets the
decoder and clock. See [decoder details](AUDIO_DECODER.md) and run the audio,
replay and physical-device checks after replacing music.

## Public builds

CI sets `SOLOMINER_PUBLIC_BUILD=1`, which always generates an empty track even
if local music exists. The firmware manifest records `celebration_audio.mode`
as `silent` or `custom-local`, plus the track hash when included. Review that
manifest before sharing a binary. Source exports never carry `private/`,
`.pio/`, or firmware binaries. Keep old Git history and backups private too.

```sh
# macOS / Linux; in PowerShell set $env:SOLOMINER_PUBLIC_BUILD = "1" first
SOLOMINER_PUBLIC_BUILD=1 pio run -e Waveshare-AMOLED-206
```

The GPL license covers software, not rights to a recording supplied by a
developer. Removing the original song from this repository does not grant
permission to distribute that song in commercial watches or other downloads.

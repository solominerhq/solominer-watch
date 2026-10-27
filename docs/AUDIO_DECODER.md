# Celebration audio decoder

The firmware uses unmodified upstream **minimp3**, pinned to commit
`ea99364f61c14656440e8d77e9c233ccf3124633`, directly under CC0. The header, license,
upstream README, source URLs and SHA-256 hashes are in `src/third_party/minimp3/`.
There is no Helix wrapper or decoder dependency. The project uses GPL-3.0-only; decoder and recording rights remain separate.

## Playback contract

- Optional local MP3, 16 kHz, mono or stereo. Mono is duplicated into the speaker's
  left/right I2S stream. Other sample rates are rejected instead of playing at
  the wrong speed. Developers can supply a local track; see [custom audio](CUSTOM_AUDIO.md).
  Without a track, no decoder task or audio initialization runs.
- A fresh decoder instance is created for every replay. Its history and PCM
  output live on the heap; decoding has no per-frame heap allocations.
- Decode input windows are limited to 16 KiB and preserve scan lookahead.
  Malformed input either advances, terminates or reports a failure.
- The audio clock starts only after the first successful PCM write. Mute keeps
  decoding and the animation clock running; the codec volume remains 69/100.
- On normal completion, silence is queued through one DMA ring plus one buffer
  before the amplifier is disabled. This lets the final music samples finish.
  Explicit exit skips or interrupts that drain.
- Existing initialization retries, sleep coordination and replay handling remain.
  Allocation, decoder and output failures leave playback restartable.

## Memory and validation

The audio task reserves 32 KiB of stack, increased from 16 KiB. Xtensa compiler
stack-usage output measured 16,608 bytes in the decoder's largest frame alone;
the task also needs room for nested calls and driver work. The decoder object
uses roughly 11 KiB of heap while playing. Firmware static RAM size does not
include these runtime allocations.

Host tests cover real mono/stereo decoding, replay, unsupported sample rates,
window boundaries, truncated and malformed input. Playback tests exercise the
production task with simulated hardware, including mute, stop, initialization
failure, output failure, draining and replay. Run:

```sh
python tests/run_tests.py
python -m unittest discover -s tests -p 'test_*.py'
pio run -e Waveshare-AMOLED-206
```

The original synthetic tone fixtures and generation commands are under
`tests/fixtures/audio/`. No private production recording is a test fixture or part of the public source.
Host checks do not establish real-time performance on the watch.

## Physical acceptance still required

On battery and USB, play the entire celebration at 69/100. Check for pauses,
distortion, animation drift or a cut-off ending. Mute/unmute across all three
pages, exit during the song, and replay at least three times. Repeat after
Battery Saver sleep/wake, with normal Wi-Fi and mining active. Check diagnostics
for audio failures or resets. If any regression appears, restore a known-good
application image and retain diagnostics for investigation.

Custom recording rights remain separate from the decoder license.

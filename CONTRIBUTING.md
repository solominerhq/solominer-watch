# Contributing

Start with README.md and docs/ARCHITECTURE.md. The supported target is the
Waveshare ESP32-S3-Touch-AMOLED-2.06. Keep changes small and describe the user-visible
problem, final behavior, and validation in your pull request.

## Local checks

```sh
python -m pip install -r requirements-build.txt
pio pkg install -e Waveshare-AMOLED-206
python tests/run_tests.py
python -m unittest discover -s tests -p 'test_*.py'
pio run -e Waveshare-AMOLED-206
```

The portable tests require a C++11 compiler (`c++` or the compiler in `CXX`). On
Windows, use WSL or another environment providing that compiler. They run the real
policy/config parsing code with a small host String adapter, not a simulated ESP32.
They cover state transitions, wrap-safe deadlines, authorization responses,
intentional pauses, config migration, display status and firmware packaging.
They do not emulate Wi-Fi, battery hardware, LVGL or SHA peripherals.

CI runs these checks on proposed changes and builds from a checkout without a
preexisting project dependency cache. The manual release workflow checks `release-readiness.json` before building a draft after tests;
a maintainer reviews and publishes it separately. Do not tag a development candidate
as a release until hardware testing and asset/dependency notices are resolved.

## Adding features

- Use the documented owner for device power, network, mining, display or settings.
- Put settings defaults/validation in `settings/` and test both config formats.
- Keep LVGL operations behind the existing display locks.
- Do not perform blocking network work in mining event callbacks.
- Add tests for meaningful behavior and failures, especially state transitions.
- Update the README if controls or normal behavior change.
- Include required generated artwork C/H assets in the source tree; a clean build cannot
  depend on untracked files from a developer's machine.
- Keep credentials, logs, `.pio/`, generated firmware, private branding originals
  and unrelated design files out of commits. Check media rights before adding assets.

Use docs/HARDWARE_TESTS.md for changes touching power, networking or controls.
Report firmware version, board revision, phone/iOS version, reproduction steps
and sanitized logs for bugs. Remove passwords, wallet addresses and SSIDs before
posting logs publicly.

## Licensing

New SOLOMINER code contributions use GPL-3.0-only; preserve all upstream notices.
See [licensing](LICENSING.md). Keep custom music under `private/celebration/`;
never contribute a recording or generated audio header. See [custom audio](docs/CUSTOM_AUDIO.md).

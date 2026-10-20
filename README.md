<p><img src=".github/wordmark.svg" alt="SOLOMINER" width="360"></p>

# SoloMiner Watch

**Wear the Network.**

[![Firmware checks](https://github.com/solominerhq/solominer-watch/actions/workflows/check.yml/badge.svg)](https://github.com/solominerhq/solominer-watch/actions/workflows/check.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-orange.svg)](LICENSE)

Bitcoin solo-mining firmware for the **Waveshare ESP32-S3-Touch-AMOLED-2.06**,
built on [NerdMiner v2](https://github.com/BitMaker-hub/NerdMiner_v2).

The watch performs real SHA-256 hashing on its ESP32-S3 and connects to a
compatible solo-mining pool over Wi-Fi. It is a mining client, not a Bitcoin
full node. Finding a block is extremely unlikely; this is a project for
learning, experimenting, and enjoying Bitcoin on your wrist.

> **v3.0.0 · public source preview.** Build, inspect, and customize the firmware.
> The default celebration is silent; [custom music](docs/CUSTOM_AUDIO.md) is optional.
> Prebuilt firmware releases follow the [release checklist](docs/OPEN_SOURCE_RELEASE.md).

## What it does

- Clock, live mining statistics, device information, and touch-friendly settings.
- Phone-based setup for Wi-Fi, pool, Bitcoin receiving address, and timezone.
- One mining profile on battery and USB, with thermal and low-battery protections.
- Battery Saver pauses mining and Wi-Fi after three minutes of physical inactivity
  on battery, then uses light sleep. Touch or a button wakes the watch.
- Three-page Block Found celebration with optional custom music, block reward, and explorer QR.
  The demo uses an example block and does not change real mining records.
- Firmware updates through the setup portal and a two-button setup recovery shortcut.

See the [user guide](docs/USER_GUIDE.md) for precise controls and behavior,
and the [v3.0.0 release notes](docs/RELEASE_v3.0.0.md) for changes.

## Supported hardware

| Component | Required hardware |
|---|---|
| Board | Waveshare ESP32-S3-Touch-AMOLED-2.06 |
| Display | 410 × 502 AMOLED touchscreen |
| Memory | 32 MB flash, 8 MB PSRAM |
| Network | 2.4 GHz Wi-Fi with internet access |
| Connection | USB-C data cable for initial flashing |

This build is specific to the 2.06-inch board. The `esp32-s3-devkitc-1`
PlatformIO identifier is a build base with board-specific overrides, not a
claim that other ESP32-S3 boards are compatible.

[Waveshare hardware documentation](https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm)

## Build

Install Python 3.11 and Git. Run these commands from this folder:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-build.txt
pio pkg install -e Waveshare-AMOLED-206
pio run -e Waveshare-AMOLED-206
```

On Windows, use `python` and activate with `.venv\Scripts\activate.bat` in
Command Prompt, or `.venv\Scripts\Activate.ps1` in PowerShell.
The first build downloads dependencies pinned in `platformio.ini`.
A source ZIP works without Git history or tags.

Build outputs appear in `firmware/v3.0.0/`. For a setup-portal update, use
**`SOLOMINER-v3.0.0.bin`**, the application image. Use the merged factory image
only for the documented USB installation procedure on the supported board;
do not upload that image through the portal. See [flashing and setup](docs/USER_GUIDE.md).

To upload directly over USB:

```sh
pio run -e Waveshare-AMOLED-206 -t upload
pio device monitor -b 115200
```

## First setup

Open **Settings → Wi-Fi & setup** and follow the watch's QR instructions.
The default setup network is `SOLOMINER`, with password `minebitcoin`;
the portal is at `http://192.168.4.1` while connected to that network.

Choose your Wi-Fi network, timezone, compatible pool, and **Bitcoin receiving
address**. The watch never needs your wallet's seed phrase or private key.
Save, then allow it to connect. Saved settings remain on the device.

If you need to return to setup, hold **BOOT and PWR together for five seconds**,
then release. This software shortcut preserves settings; a fully frozen
application may require the board's USB download-mode recovery instead.

## Development

```sh
python tests/run_tests.py
python -m unittest discover -s tests -p 'test_*.py'
```

Install PlatformIO dependencies first. Host tests also require a C++11 compiler
(`c++`, or set `CXX`). On Windows, WSL is an option for those tests.
Host tests do not replace testing Wi-Fi, battery behavior, touch, and audio
on the physical watch.

| Guide | Purpose |
|---|---|
| [User guide](docs/USER_GUIDE.md) | Controls, setup, updates, and troubleshooting |
| [Architecture](docs/ARCHITECTURE.md) | Firmware modules and ownership |
| [Contributing](CONTRIBUTING.md) | Development and validation |
| [Hardware tests](docs/HARDWARE_TESTS.md) | Checks before a release |
| [Diagnostics](docs/DIAGNOSTICS.md) | Collecting useful, sanitized diagnostics |
| [Security](SECURITY.md) | Reporting vulnerabilities privately |
| [Release preparation](docs/OPEN_SOURCE_RELEASE.md) | Publication decisions and source export |
| [Third-party notices](THIRD_PARTY_NOTICES.md) | Dependencies, fonts, and media |

## Custom celebration music

The public build works without a song. To add your own:

```sh
python scripts/set_celebration_audio.py "/path/to/my-song.mp3"
pio run -e Waveshare-AMOLED-206
```

The recording stays in an ignored local folder. The import tool converts other
formats with FFmpeg when needed; no player code edits are required. See
[custom audio](docs/CUSTOM_AUDIO.md) for replacement, removal and timing controls.

## Credits and licensing

The combined firmware and SOLOMINER-authored modifications use **GPL-3.0-only**.
See [LICENSE](LICENSE) and [licensing scope](LICENSING.md). Inherited MIT notices
from NerdMiner/Bitmaker and HAN Miner are preserved in `licenses/Inherited-MIT.txt`;
dependencies and assets retain their own terms. See
[third-party notices](THIRD_PARTY_NOTICES.md) and [asset provenance](docs/ASSET_PROVENANCE.md).
No commercial song is included or downloaded by the public build.

[SOLOMINER](https://getsolominer.com) · [@solominerhq](https://x.com/solominerhq)

### Reproducible assets and dependencies

The exact framework package is pinned. See [dependency audit](docs/DEPENDENCY_AUDIT.md)
for linked components, notices and the companion source snapshot. Current fonts
and images have included source assets and optional regeneration scripts; see
[asset sources](docs/ASSET_PROVENANCE.md). Binary firmware distribution has
additional dependency and hardware checks listed in the release checklist.

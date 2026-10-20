# Third-party code and media

The combined firmware uses GPL-3.0-only. Inherited MIT and other third-party
permissions remain in force; their notices are preserved. See LICENSING.md.

## Direct dependencies

Versions are pinned in `platformio.ini`. The copies under `licenses/` supplement,
and do not replace, the original file-level notices and upstream terms.

| Component | Version | Notice / terms |
|---|---|---|
| NerdMiner v2 / HAN Miner | Inherited source | MIT notice in `licenses/Inherited-MIT.txt`; retain file-level notices. |
| Blockstream Jade SHA | Inherited via NerdMiner | MIT, `licenses/Blockstream-Jade-MIT.txt`; original attribution retained |
| Arduino ESP32 core | 2.0.17 | LGPL-2.1-or-later, `licenses/Arduino-ESP32-LGPL-2.1.md`; preserve package notices and review linked SDK components |
| ArduinoJson | 6.21.6 | MIT, `licenses/ArduinoJson.txt` |
| WiFiManager | 2.0.17 | MIT, `licenses/WiFiManager.txt` |
| OneButton | 2.6.2 | BSD-3-Clause, `licenses/OneButton.txt` |
| NTPClient | 3.2.1 | MIT, `licenses/NTPClient.txt`, copied from its source header |
| GFX Library for Arduino | 1.6.4 | Upstream BSD notice, `licenses/Arduino-GFX.txt`; also retain component notices |
| LVGL | 9.3.0 | MIT, `licenses/LVGL.txt` |
| XPowersLib | 0.2.6 | MIT, `licenses/XPowersLib.txt` |
| minimp3 | `ea99364f61c14656440e8d77e9c233ccf3124633` | CC0 1.0, `src/third_party/minimp3/LICENSE` and `licenses/minimp3-CC0.txt` |
| Inter font subsets | 4.001 (`git-66647c0bb`) | Exact TTFs and manifest in `assets/fonts`; original SIL OFL 1.1 notice in `licenses/Inter-OFL.txt` |

The actual linked archives and exact source/dependency snapshot are now recorded
in [the dependency audit](docs/DEPENDENCY_AUDIT.md) and its JSON inventory. License
supplements cover IDF components, Newlib, GCC runtime exception, LVGL QR/allocator/
snprintf components, Montserrat and Font Awesome. Full package/file notices remain
in the source snapshot. Binary-only Espressif SDK treatment is still under review;
these copies are not a blanket legal clearance or a file-complete SPDX inventory.

The GFX notice was obtained from the pinned upstream tag:
https://github.com/moononournation/Arduino_GFX/blob/v1.6.4/license.txt

## Audio distribution decision

The firmware now uses the [upstream minimp3 decoder](https://github.com/lieff/minimp3)
directly. Its unmodified header and CC0 license are committed under
`src/third_party/minimp3/`, with the exact source commit and hashes in
`UPSTREAM.json`. The Helix wrapper and decoder are no longer dependencies or
linked into the firmware. This resolves that specific decoder licensing obstacle;
it does not relicense the rest of the project. See [decoder notes](docs/AUDIO_DECODER.md).

The public source and default binary contain no commercial song. Private music
may be supplied locally; neither the GPL nor CC0 decoder license grants rights
to that recording. Obtain suitable rights for any recording before distributing
firmware that contains it.
See [custom audio](docs/CUSTOM_AUDIO.md).

## Artwork and fonts

See [asset provenance](docs/ASSET_PROVENANCE.md) and
[release preparation](docs/OPEN_SOURCE_RELEASE.md). Preserve required font and
artwork notices. The original permission records must support redistribution,
not only use in marketing. The GPL decision does not relicense third-party assets.

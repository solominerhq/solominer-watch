# Dependencies

The firmware pins its toolchain and direct libraries in `platformio.ini`.
[Third-party notices](../THIRD_PARTY_NOTICES.md) and `licenses/` retain their
license texts and attribution. The application is based on NerdMiner v2;
SOLOMINER code uses GPL-3.0-only as described in [LICENSING.md](../LICENSING.md).

## Build components

| Component | Version or source | License |
|---|---|---|
| PlatformIO Espressif32 | 6.11.0 | Component-specific |
| Arduino ESP32 | 2.0.17; package `3.20017.241212+sha.dcc1105b` | LGPL-2.1-or-later and file notices |
| ESP-IDF SDK | `38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`, 4.4.7 | Apache-2.0 and component-specific terms |
| ArduinoJson, WiFiManager, NTPClient, LVGL, XPowersLib | Pinned in `platformio.ini` | MIT |
| OneButton | Pinned in `platformio.ini` | BSD-3-Clause |
| Arduino GFX | Pinned version plus the included build patch | BSD and component-specific terms |
| ES8311 audio driver | Espressif/Waveshare implementation with SOLOMINER adaptation | Apache-2.0 |
| minimp3 | Vendored, pinned decoder | CC0 |
| Inter fonts | 4.001 | SIL OFL-1.1 |
| LVGL fonts and utilities | Included with the pinned LVGL version | See `licenses/` |

The [dependency inventory](dependency-inventory.json) records package metadata
and linked archive hashes from the reference build. The [upstream source list](dependency-upstreams.json)
records source URLs and checksums. Regenerate the inventory when dependencies change.

## SDK source and binary components

The ESP32 SDK includes binary-only radio/PHY libraries. Their license texts do
not make their implementations available as source. The SDK reports
`4.4.7-dirty`, reflecting vendor build patches; the pinned Arduino package also
contains changes relative to its upstream tag.

An independent rebuild of the entire SDK has not been verified. The GPL treatment
of the binary-only SDK components remains unresolved for a combined firmware
release; no additional linking permission has been adopted. Application source
publication does not resolve that distribution question. See
[licensing](../LICENSING.md) and [releases](RELEASING.md).

## Dependency snapshots

After building, prepare a local snapshot with:

```sh
python scripts/export_dependencies.py --output dist
```

The archive contains the exact installed and patched direct libraries, Arduino
source, ESP32-S3 SDK materials, PlatformIO build scripts, pinned upstream source,
license notices and checksums. It includes vendor `.a` libraries; it is not a
claim that the entire SDK can be rebuilt from public source. It excludes the
application firmware, custom music, saved device settings and Git history.

The script validates downloaded upstream archives against committed SHA-256
hashes. Public firmware packaging requires the private maintainer approval
process described in [releases](RELEASING.md).

## Upstream references

- [Arduino SDK update](https://github.com/espressif/arduino-esp32/pull/9339)
- [ESP-IDF component licenses](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/COPYRIGHT.html)
- [Pinned ESP-IDF source](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3)
- [Pinned Wi-Fi libraries](https://github.com/espressif/esp32-wifi-lib/tree/f2aae4d44ec7908013066e69d29b9948846c335c)
- [LVGL font licenses](https://github.com/lvgl/lvgl/tree/v9.3.0/scripts/built_in_font/font_license)

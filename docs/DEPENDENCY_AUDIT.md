# Dependency and corresponding-source audit — 2026-10-20

## Scope

The default public candidate builds without a commercial recording and uses
GPL-3.0-only for SOLOMINER code, with inherited and dependency terms preserved.
The direct dependencies, actual linked archives, SDK source revision, fonts and
notices have now been traced. The review snapshot is archived separately from
the small GitHub source tree. This audit is not a declaration of legal clearance
for the combined firmware: the binary-only SDK issue below remains open.

The build was inspected through its link map, not just `lib_deps`. It selects
72 archives; 63 have positive nonzero allocated section contributions identified
in the map. ArduinoJson and minimp3 are compiled through application objects.
See [the machine-readable inventory](dependency-inventory.json) for exact hashes,
package metadata, conservative archive selection and dependency ZIP checksum.

## Exact sources and licenses

| Layer | Verified source / terms | Materials retained |
|---|---|---|
| Application | SOLOMINER GPL-3.0-only; inherited NerdMiner/HAN/Jade MIT | Application source, notices, build/installation scripts |
| Arduino ESP32 | Package `3.20017.241212+sha.dcc1105b`, core 2.0.17; LGPL-2.1-or-later and file notices | Exact installed cores/libraries/variants, including seven differences from upstream commit dcc1105b; do not substitute an unpatched tag |
| ESP-IDF | `38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`, v4.4.7; primarily Apache-2.0 with component exceptions | Source archive, applicable submodule archives, current ESP32-S3 SDK headers/configuration/archives, builder source/patches |
| SDK additions | esp-insights `203a87f671710295579404f7d4c62a8a5e25ae7c` via RainMaker `d8e9345`; Apache-2.0 | Exact insights source, identified through the upstream SDK update and RainMaker gitlink |
| Networking/storage/crypto | lwIP BSD; mbedTLS Apache-2.0; cJSON MIT; SPIFFS MIT; protobuf-c BSD; WPA BSD and file notices | Pinned submodule source archives; notices copied and originals retained |
| Core/runtime | FreeRTOS MIT; Newlib mixed permissive notices; GCC runtime GPL with Runtime Library Exception; Xtensa/other file terms | Full IDF component notices, Newlib collection, GCC exception; exact toolchain identity in platform configuration |
| Direct libraries | ArduinoJson, WiFiManager, NTPClient, LVGL, XPowersLib MIT; OneButton BSD-3; Arduino-GFX BSD and component terms | Exact installed source trees, including the applied GFX patch; original file-level notices |
| LVGL subcomponents | QR generator MIT; TLSF allocator and snprintf component notices; Montserrat and Font Awesome font terms | Separate license supplements copied from the pinned LVGL tree |
| ES8311 | Espressif Apache-2.0, derived from Waveshare sample | Original headers, full Apache text; SOLOMINER I2C adaptation explicitly marked |
| Audio decoder | minimp3 pinned CC0 | Vendored source and source hash; no Helix dependency |
| Inter | Original Inter 4.001, SIL OFL-1.1 | Exact source TTFs, original license, embedded notices, subset manifest and regeneration script |

The SDK header identifies IDF commit `38eeba213a` and branch `v4.4.7`.
`libesp_system.a` reports `v4.4.7-dirty`, consistent with the library builder's
patching stage. The historical builder commit available at the SDK update is
`956a355d51e7f4c3a454e193a9c28fcf321a452f`; its scripts/patches are archived.
This is source tracing, not an independent proof that rebuilding the entire SDK
produces identical archives. The seven Arduino source differences are captured
in the exact installed-source snapshot and recorded in the inventory.

## Binary-only Espressif components: still open

The ESP32 SDK ships some radio/PHY code only as compiled libraries. The relevant
upstream repositories supply Apache-2.0 license texts; that does **not** mean the
library implementations have publicly supplied source. Wi-Fi archives were
matched byte for byte against the IDF-pinned `esp32-wifi-lib` commit. The snapshot
preserves these vendor archives and their terms without pretending they are
application source.

Before publishing combined GPL firmware, resolve whether these components qualify
for the GPL system-library treatment or require an appropriate linking permission.
No additional linking permission has been adopted. A project permission cannot
grant rights on behalf of Arduino or other copyright holders.
`dependency_distribution` stays false pending this resolution and a
final check that the matching release materials satisfy the chosen distribution
method.

## Snapshot and reproducibility

The companion `SOLOMINER-v3.0.0-dependencies-PRIVATE-REVIEW.zip` contains:

- Exact direct-library source trees, including the GFX modifications.
- Exact Arduino source and the ESP32-S3 SDK material used in the build.
- Platform build scripts and pinned upstream IDF/component source archives.
- Original and supplemental notices, upstream-download URLs, and SHA-256 hashes.

It deliberately includes vendor `.a` libraries, but no application firmware,
commercial recording, local settings, credentials or Git history. Its identity is
in `dependency-inventory.json`. Do not use the private music build as a public
release. Do not label this snapshot a completely source-buildable SDK: binary-only
vendor components and the independent SDK rebuild review remain as stated above.

To regenerate the dependency snapshot after the build (downloads are checked
against committed SHA-256 hashes):

```sh
python scripts/export_dependencies.py --output dist
python scripts/export_source.py --output dist
```

The draft-release workflow now attaches both archives alongside the firmware.
Its existing readiness check blocks publication while the unresolved items remain.

For normal application builds:

```sh
python -m pip install -r requirements-build.txt
pio pkg install -e Waveshare-AMOLED-206
python tests/run_tests.py
python -m unittest discover -s tests -p 'test_*.py'
SOLOMINER_PUBLIC_BUILD=1 pio run -e Waveshare-AMOLED-206
```

The framework package is now pinned explicitly. A clean machine should also be
used for the final release acceptance; compare the resulting dependency inventory
and retain the exact source/firmware manifests. Building the application with
prebuilt SDK libraries is different from independently rebuilding that SDK.

## Distribution checklist

1. Resolve the binary-only SDK licensing point for the intended firmware distribution.
2. Build the final public version with silent audio, test it on hardware, and record
   its exact commit and firmware hash.
3. Distribute matching application/dependency source, notices and required
   installation/relinking information using a GPL-compliant method. The source
   tree provides the complete application and Arduino sources, not only object
   files; do not rely on a link to a moving branch.
4. Attach both source archives and the release manifest beside binary downloads.
   Ensure shipped devices receive the appropriate source-access information too.
5. Keep the release workflow blocked until these decisions and tests are recorded.

## Evidence

- [Arduino SDK update #9339](https://github.com/espressif/arduino-esp32/pull/9339)
- [ESP-IDF 4.4.7 copyright/component overview](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/COPYRIGHT.html)
- [ESP-IDF pinned source](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3)
- [Pinned Wi-Fi library repository](https://github.com/espressif/esp32-wifi-lib/tree/f2aae4d44ec7908013066e69d29b9948846c335c)
- [LVGL 9.3.0 font license records](https://github.com/lvgl/lvgl/tree/v9.3.0/scripts/built_in_font/font_license)
- GPLv3 sections 1, 6 and 7 in the included LICENSE; Arduino LGPL text in licenses/.

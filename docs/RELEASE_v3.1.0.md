# SoloMiner Watch v3.1.0

## Interface updates

- Brightness and Battery Saver can be changed directly from Settings.
- Startup and charging use plain black backgrounds. The charging ring is thicker.
- The Block Found countdown has a larger, two-line title and a Cancel button.
- Block Found has compact outline sound and close icons with separate dark backings.
- Sound and close have larger touch areas and remain available from the opening
  animation frame through all three celebration screens. Swipes navigate without
  activating the buttons.
- Swipes and the top BOOT button can switch celebration pages from the opening
  frame. Manual navigation cancels the automatic page tour and music continues.

The astronaut landing sequence is retained. Routine application updates are
designed to preserve saved settings. Normal display timing is 60 seconds to
dim and 65 seconds to turn off.

## Firmware files

Supported hardware: **Waveshare ESP32-S3-Touch-AMOLED-2.06** with the existing
32 MB partition layout. Builds write to `firmware/v3.1.0/`.

| File | Purpose |
|---|---|
| `SOLOMINER-v3.1.0.bin` | Application image for updates through the phone setup portal. |
| `Waveshare-AMOLED-206_firmware.bin` | The same application image under the build target name. |
| `Waveshare-AMOLED-206_factory.bin` | Merged image for the documented USB installation procedure. Do not upload it through the portal. |
| `Waveshare-AMOLED-206_manifest.json` | Version, file sizes and SHA-256 checksums. |

The public source builds without music by default; see [custom audio](CUSTOM_AUDIO.md)
to supply a recording. See [phone update instructions](FIRMWARE_UPDATE.md),
the [user guide](USER_GUIDE.md), and
[v3.0.0 notes](RELEASE_v3.0.0.md) for the earlier feature overview.

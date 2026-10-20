# Celebration artwork

SOLOMINER-supplied source artwork for the Block Found sequence, included under
GPL-3.0-only. See [asset sources](../../docs/ASSET_PROVENANCE.md).

- `astronaut-poses.png`: source sheet for the landing and flag-strike sprites.
- `moon-transparent-20261020.png`: transparent lunar foreground; the starfield
  is drawn separately by the firmware.
- `mission_block.png` and `mission_found.png`: fixed-word title masks, originally
  rendered using Arial Bold at 66 px. No Arial font file is included.

`python scripts/generate_artwork.py --check` verifies all generated celebration
arrays. Remove `--check` to regenerate. The generator contains the crop rectangles,
scales, and pixel-format conversion. Normal builds require no image tools.

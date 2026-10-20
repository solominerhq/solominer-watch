# Artwork and font sources

SOLOMINER supplies the artwork in this repository and has authorized its inclusion
in the public project. Software and supplied artwork use GPL-3.0-only as described
in [LICENSING.md](../LICENSING.md); third-party fonts retain their own licenses.
SOLOMINER names and logos do not grant an endorsement or trademark license.

| Asset | Editable or source material |
|---|---|
| Splash astronaut | `assets/splash/astronaut.png`, 200 × 200 RGBA |
| Celebration poses | `assets/celebration/astronaut-poses.png` |
| Lunar foreground | `assets/celebration/moon-transparent-20261020.png` |
| BLOCK / FOUND title masks | Fixed-word PNGs in `assets/celebration/`; no Arial font file is distributed |
| Starfield | Procedural drawing in `src/ui/clock_stars.h` |
| SOLOMINER wordmark | Outlined SVG in `assets/branding/` |
| Inter | Inter 4.001 source TTFs, OFL notice, subset mapping, and hashes in `assets/fonts/` |
| Montserrat / Font Awesome | Pinned LVGL font resources; corresponding notices in `licenses/` |
| Audio fixtures | Original CC0 synthetic test tones; no commercial recording |

## Reproduce generated assets

Normal firmware builds use committed C arrays. To verify or regenerate them:

```sh
python -m pip install Pillow
npm install --prefix tools/font-converter lv_font_conv@1.5.3
python scripts/generate_fonts.py --converter tools/font-converter/node_modules/lv_font_conv/lv_font_conv.js --check
python scripts/generate_artwork.py --check
```

Remove `--check` to regenerate. Image crops, alpha conversion, and output formats
are defined in `scripts/generate_artwork.py`. Portal assets use
`scripts/build_setup_portal.py` separately. Preserve font and upstream notices
when changing or redistributing assets.

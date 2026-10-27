# Inter font source

These unmodified TTF files came from the original project's Inter download.
Their embedded version is 4.001, git-66647c0bb. The original accompanying
OFL notice is included. Embedded copyright notices differ in year from the
folder notice; both are preserved. See MANIFEST.json for exact hashes.

Seventeen subsets reproduced their original bitmap bytes and glyph metrics
with lv_font_conv 1.5.3. The old cloud-generated 36/40/42 bold subsets had
no recoverable exact input; they now use the included Inter 28pt SemiBold.
Internal C symbol names are retained for source compatibility. The TTF files
are unchanged. Font licenses remain OFL-1.1, separate from firmware GPLv3.

Regenerate with `python scripts/generate_fonts.py --converter /path/to/lv_font_conv.js`.
Install the pinned converter with `npm install --prefix tools/font-converter lv_font_conv@1.5.3`.

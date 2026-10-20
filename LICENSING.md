# Licensing and distribution

## Project license

Copyright (c) 2025–2026 SOLOMINER.

The SoloMiner firmware as a combined work, and SOLOMINER-authored modifications
in this version, are licensed under **GNU GPL version 3 only**
(`GPL-3.0-only`). See the full text in [LICENSE](LICENSE).

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for details.

## Inherited code and separate assets

- NerdMiner v2, HAN Miner and other inherited components retain their existing
  notices and permissions. The previous root MIT notice is preserved verbatim
  in `licenses/Inherited-MIT.txt`. File-specific notices still apply.
- The MIT-derived SHA implementation retains Bitmaker and Blockstream Jade
  attribution; see `THIRD_PARTY_NOTICES.md`. GPL does not erase those notices.
- Dependencies retain their MIT, BSD, Apache, LGPL, CC0 and other licenses.
  Font subsets retain their font licenses. Consult the component notices.
- No music recording is included in the default source/build, apart from
  CC0 synthetic decoder-test tones. Custom recordings have separate rights.
- SOLOMINER-supplied artwork is included under GPL-3.0-only. The software and
  artwork license does not grant permission to imply endorsement through
  SOLOMINER names or logos. See `docs/ASSET_PROVENANCE.md` for asset sources.
- Any copies previously received under MIT remain usable under those grants.
  This license decision does not retroactively withdraw them.

## Distributing firmware or watches

You may modify, fork and sell GPL-covered firmware and devices. When conveying
covered binaries, comply with GPLv3, including providing the corresponding
source by an allowed method and preserving notices. For a download, host the
matching source alongside it. Source must match the released binary, including
modifications and scripts needed to build/install it; a link to a moving branch
alone is not a complete release process. If installation information is required
for a User Product under GPLv3 section 6, provide it too. Do not add restrictions
that prevent recipients exercising their GPL rights.

The Arduino core is LGPL-2.1-or-later and the Espressif SDK includes separately
licensed components. A source tree that downloads pinned dependencies is useful
for reproducibility but is not, by itself, a completed corresponding-source or
LGPL relinking review. Before distributing release binaries, inventory the exact
linked components, include the necessary sources/notices and any required
relinking materials, and document the matching toolchain and installation steps.
The technical inventory and companion source snapshot are now prepared in
`docs/DEPENDENCY_AUDIT.md`. The binary-only Espressif SDK licensing point remains
open. No additional linking permission has been adopted.
See `docs/OPEN_SOURCE_RELEASE.md` for remaining release work.

Contributions to SOLOMINER-authored software are accepted under GPL-3.0-only
unless explicitly identified as a separately licensed third-party contribution.
Do not change upstream license headers or contribute recordings without rights.

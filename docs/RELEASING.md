# Releases

The repository provides buildable source. Prebuilt firmware downloads are not
currently provided. See [licensing](../LICENSING.md) and
[dependencies](DEPENDENCIES.md) before distributing compiled firmware.

## Source archives

```sh
python scripts/export_source.py --public --output dist
```

The export includes application source, artwork, fonts, tests, build scripts and
license notices, plus a SHA-256 manifest. It rejects unreviewed audio and common
credential patterns, and excludes Git history, private files and build outputs.
CI checks the export and scans both the working tree and Git history.

## Firmware releases

1. Build the exact release commit with `SOLOMINER_PUBLIC_BUILD=1` and run the
   automated tests and [hardware checks](HARDWARE_TESTS.md).
2. Verify the firmware manifest, silent-audio setting and supported board.
3. Prepare matching application and dependency source, notices and required
   installation/relinking information. Follow [licensing](../LICENSING.md).
4. Run **Prepare draft firmware release** in GitHub Actions. Review the draft's
   files, checksums and release notes before publishing it.

The workflow requires a private approval record and stops if it is missing or
incomplete. Maintainers supply it through the repository Actions secret
`SOLOMINER_RELEASE_APPROVAL_JSON`. Local packaging reads the same JSON from
`private/release-approval.json`, which is ignored by Git and excluded from exports.

The record contains `music_redistribution`, `dependency_distribution`,
`asset_provenance` and `hardware_acceptance`. Each entry needs `approved: true`
and a nonempty `evidence` string after that check is complete. Keep supporting
review notes private; never commit the record or print it in build logs.
Approval does not replace the technical or licensing checks themselves.

For local packaging:

```sh
python scripts/check_release_approval.py
python scripts/export_dependencies.py --public --output dist
python scripts/export_source.py --public --output dist
```

Use source archives that match the exact firmware binary. A moving branch alone
is not a substitute for the corresponding source of a released build. Never
publish a locally supplied recording without the necessary redistribution rights.

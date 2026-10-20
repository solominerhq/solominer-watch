# Release process

The public repository contains application source, artwork, font sources, tests,
build scripts, and license notices. It does not contain commercial music, local
configuration, old development history, or downloadable firmware binaries.

## Before publishing firmware

1. Review the exact linked dependencies and matching source materials. The
   binary-only Espressif radio/PHY component distribution review remains pending;
   see [dependency inventory](DEPENDENCY_AUDIT.md). No extra linking exception
   has been adopted.
2. Test the final commit on the supported watch using
   [hardware acceptance checks](HARDWARE_TESTS.md). Record the firmware hash,
   board revision, test results, and known limitations.
3. Record supported decisions in `release-readiness.json`. Source publication
   requires the music-exclusion and artwork entries; firmware/dependency releases
   also require dependency-distribution and hardware-acceptance entries.
4. Build with `SOLOMINER_PUBLIC_BUILD=1`, verify the firmware manifest says
   `celebration_audio.mode: silent`, and review every attached file.
5. Supply matching application and dependency source, notices, and required
   installation/relinking information with the release. Do not substitute a
   moving branch for the source corresponding to a specific binary.

The manual GitHub workflow prepares a **draft** only after its readiness check,
tests, and build pass. A maintainer must review and publish that draft separately.

## Application source export

```sh
python scripts/export_source.py --public --output dist
```

The archive contains application source and a SHA-256 manifest. It excludes Git
history, local settings, private music, firmware binaries, and build caches.
A review-only export without `--public` remains available during development.

## Matching dependency export

After final release approval and a clean build:

```sh
python scripts/check_release_readiness.py
python scripts/export_dependencies.py --public --output dist
```

This captures the pinned dependency sources and vendor materials described in
[the dependency inventory](DEPENDENCY_AUDIT.md). It is separate from the application
source archive. Public application source does not imply that every vendor SDK
component has publicly available implementation source.

## Publication hygiene

- Run the source export checks and full-history secret scan before publishing.
- Keep private recordings, credentials, diagnostic logs, agreements, review notes,
  and local backups outside the repository.
- Preserve GPL, inherited copyright, font, and third-party notices.
- Never attach firmware containing a locally supplied recording without permission.
- Use a fresh reviewed source history when creating a public repository from
  private development. Removing a file in a later commit does not erase it from history.

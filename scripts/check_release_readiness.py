"""Check recorded publication decisions; this does not grant rights."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    "music_redistribution",
    "dependency_distribution",
    "asset_provenance",
    "hardware_acceptance",
)
SOURCE_REQUIRED = ("music_redistribution", "asset_provenance")


def check_readiness(root=ROOT, *, source_only=False):
    record = json.loads((Path(root) / "release-readiness.json").read_text())
    pending = []
    for key in SOURCE_REQUIRED if source_only else REQUIRED:
        item = record.get(key, {})
        if not isinstance(item, dict):
            pending.append(key)
            continue
        evidence = item.get("evidence", "")
        if (item.get("approved") is not True or
                not isinstance(evidence, str) or not evidence.strip()):
            pending.append(key)
    if pending:
        raise ValueError("Public " + ("source" if source_only else "firmware") +
                         " release not cleared: " + ", ".join(pending) +
                         ". See docs/OPEN_SOURCE_RELEASE.md.")


if __name__ == "__main__":
    try:
        check_readiness()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
    print("Recorded publication decisions complete. Review final files before publishing.")

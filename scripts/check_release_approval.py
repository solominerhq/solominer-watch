"""Require a private maintainer approval record before packaging firmware."""
from pathlib import Path
import json
import os

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    "music_redistribution",
    "dependency_distribution",
    "asset_provenance",
    "hardware_acceptance",
)


def check_approval(root=ROOT):
    # CI supplies a repository secret. Local maintainers use an ignored file.
    # Neither the record nor its contents are printed or included in an export.
    raw = os.environ.get("SOLOMINER_RELEASE_APPROVAL_JSON")
    if raw is None:
        try:
            raw = (Path(root) / "private/release-approval.json").read_text()
        except OSError:
            raise ValueError("Firmware release approval is missing. See docs/RELEASING.md.") from None
    try:
        record = json.loads(raw)
    except (ValueError, TypeError):
        raise ValueError("Firmware release approval is invalid.") from None
    if not isinstance(record, dict):
        raise ValueError("Firmware release approval is invalid.")
    for key in REQUIRED:
        item = record.get(key)
        if not isinstance(item, dict):
            raise ValueError("Firmware release approval is incomplete.")
        evidence = item.get("evidence")
        if (item.get("approved") is not True or
                not isinstance(evidence, str) or not evidence.strip()):
            raise ValueError("Firmware release approval is incomplete.")


if __name__ == "__main__":
    try:
        check_approval()
    except ValueError as error:
        raise SystemExit(str(error))
    print("Firmware release approval verified.")

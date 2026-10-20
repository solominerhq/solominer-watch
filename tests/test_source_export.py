"""Prevent incomplete exports, credential leakage, and accidental release bypass."""
from pathlib import Path
from unittest import TestCase
import json
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from export_source import source_files, check_private_material
from check_release_readiness import check_readiness, REQUIRED, SOURCE_REQUIRED


class SourceExportTests(TestCase):
    def test_asset_inputs_and_probe_config_ship_with_source(self):
        names = {p.relative_to(ROOT).as_posix() for p in source_files()}
        for name in ("assets/branding/solominer-wordmark.svg",
                     "assets/celebration/moon-transparent-20261020.png",
                     "assets/setup-portal/index.html", "platformio.sleep-test.ini",
                     "docs/USER_GUIDE.md", "SECURITY.md", "release-readiness.json"):
            self.assertIn(name, names)
        self.assertFalse(any(n.startswith((".git/", ".pio/", "firmware/")) for n in names))

    def test_high_confidence_credentials_are_rejected_without_echoing_them(self):
        for secret in (b"github_" + b"pat_" + b"a" * 60,
                       b"ghp" + b"_" + b"a" * 40,
                       b"-----BEGIN " + b"PRIVATE KEY-----"):
            with self.assertRaises(ValueError) as error:
                check_private_material(secret, "example.txt")
            self.assertNotIn(secret.decode(), str(error.exception))
        check_private_material(b'{"wifi_password": "YOUR_WIFI_PASSWORD"}', "example.json")

    def test_local_paths_and_authenticated_urls_are_rejected_without_echo(self):
        for value in (b"C:" + b"\\Users\\someone\\project",
                      b"https://user:" + b"example-password@example.invalid/repo",
                      b"-----BEGIN " + b"ENCRYPTED PRIVATE KEY-----"):
            with self.assertRaises(ValueError) as error:
                check_private_material(value, "example.txt")
            self.assertNotIn(value.decode(), str(error.exception))

    def test_source_approval_cannot_bypass_firmware_approval(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "release-readiness.json"
            record = {k: {"approved": k in SOURCE_REQUIRED, "evidence": "review record"}
                      for k in REQUIRED}
            path.write_text(json.dumps(record))
            check_readiness(tmp, source_only=True)
            with self.assertRaises(ValueError):
                check_readiness(tmp)
            for key in SOURCE_REQUIRED:
                record[key]["approved"] = False
                path.write_text(json.dumps(record))
                with self.assertRaises(ValueError):
                    check_readiness(tmp, source_only=True)
                record[key]["approved"] = True

    def test_missing_false_or_unsubstantiated_approval_blocks_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "release-readiness.json"
            for bad in ({}, {k: {"approved": "true", "evidence": "review"} for k in REQUIRED},
                        {k: {"approved": True, "evidence": ""} for k in REQUIRED}):
                path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):
                    check_readiness(tmp)
            path.write_text(json.dumps({k: {"approved": True, "evidence": "review record"}
                                        for k in REQUIRED}))
            check_readiness(tmp)

    def test_review_archive_has_verified_manifest_and_no_build_output(self):
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([sys.executable, str(ROOT / "scripts/export_source.py"),
                            "--output", tmp], check=True, capture_output=True)
            archive, = Path(tmp).glob("*-PRIVATE-REVIEW.zip")
            with zipfile.ZipFile(archive) as z:
                prefix = archive.stem + "/"
                manifest = json.loads(z.read(prefix + "SOURCE_SHA256.json"))
                self.assertEqual(set(z.namelist()),
                                 {prefix + p for p in manifest} |
                                 {prefix + "SOURCE_SHA256.json"})
                for name, checksum in manifest.items():
                    self.assertEqual(hashlib.sha256(z.read(prefix + name)).hexdigest(),
                                     checksum, name)

    def test_export_rejects_directory_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src").symlink_to(ROOT / "src", target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "symlink"):
                source_files(root)

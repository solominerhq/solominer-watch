"""Prevent incomplete exports, credential leakage, and accidental release bypass."""
from pathlib import Path
from unittest import TestCase
import json
import os
from unittest.mock import patch
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from export_source import source_files, check_private_material
from check_release_approval import check_approval, REQUIRED


class SourceExportTests(TestCase):
    def test_asset_inputs_and_probe_config_ship_with_source(self):
        names = {p.relative_to(ROOT).as_posix() for p in source_files()}
        for name in ("assets/branding/solominer-wordmark.svg",
                     "assets/celebration/moon-transparent-20261020.png",
                     "assets/setup-portal/index.html", "platformio.sleep-test.ini",
                     "docs/USER_GUIDE.md", "SECURITY.md", "docs/RELEASING.md"):
            self.assertIn(name, names)
        self.assertFalse(any(n.startswith((".git/", ".pio/", "firmware/", "private/")) for n in names))

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

    def test_missing_false_or_unsubstantiated_approval_blocks_release(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {}, clear=True):
            path = Path(tmp) / "private/release-approval.json"
            with self.assertRaises(ValueError):
                check_approval(tmp)
            path.parent.mkdir()
            for bad in ({}, [], None,
                        {k: {"approved": "true", "evidence": "review"} for k in REQUIRED},
                        {k: {"approved": False, "evidence": "review"} for k in REQUIRED},
                        {k: {"approved": True, "evidence": ""} for k in REQUIRED}):
                path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):
                    check_approval(tmp)
            record = {k: {"approved": True, "evidence": "review record"} for k in REQUIRED}
            path.write_text(json.dumps(record))
            check_approval(tmp)
            for key in REQUIRED:
                incomplete = dict(record)
                incomplete.pop(key)
                path.write_text(json.dumps(incomplete))
                with self.assertRaises(ValueError):
                    check_approval(tmp)

    def test_ci_approval_stays_private_and_cannot_fall_back_to_local_file(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {}, clear=True):
            path = Path(tmp) / "private/release-approval.json"
            path.parent.mkdir()
            private_note = "confidential-example-record"
            record = {k: {"approved": True, "evidence": private_note} for k in REQUIRED}
            path.write_text(json.dumps(record))
            for raw in ("", "not-json-" + private_note, "{}", "[]"):
                with patch.dict(os.environ, {"SOLOMINER_RELEASE_APPROVAL_JSON": raw}):
                    with self.assertRaises(ValueError) as error:
                        check_approval(tmp)
                    self.assertNotIn(private_note, str(error.exception))
            path.unlink()
            with patch.dict(os.environ, {"SOLOMINER_RELEASE_APPROVAL_JSON": json.dumps(record)}):
                check_approval(tmp)
                result = subprocess.run(
                    [sys.executable, str(ROOT / "scripts/check_release_approval.py")],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0)
                self.assertNotIn(private_note, result.stdout + result.stderr)

    def test_public_source_exports_without_private_approval(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {}, clear=True):
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts/export_source.py"),
                 "--public", "--output", tmp], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            archive, = Path(tmp).glob("*-source.zip")
            with zipfile.ZipFile(archive) as z:
                names = z.namelist()
                self.assertFalse(any("/private/" in n or
                                     n.endswith(("/release-readiness.json", "/release-approval.json"))
                                     for n in names))

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

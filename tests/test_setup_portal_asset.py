"""The page embedded in firmware must exactly match its editable sources."""
from pathlib import Path
import gzip
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class SetupPortalAssetTests(unittest.TestCase):
    def test_embedded_page_matches_sources(self):
        folder = ROOT / 'assets/setup-portal'
        html = (folder / 'index.html').read_text()
        html = html.replace('/* PORTAL_CSS */', (folder / 'portal.css').read_text())
        html = html.replace('/* PORTAL_JS */', (folder / 'portal.js').read_text())
        header = (ROOT / 'src/network/setup_portal_asset.h').read_text()
        payload = bytes(int(byte, 16) for byte in re.findall(r'0x([0-9a-f]{2})', header))
        self.assertEqual(gzip.decompress(payload), html.encode(),
                         'Run python3 scripts/build_setup_portal.py before building.')
        self.assertNotRegex(html, r'<(?:script|link|img)\b[^>]*(?:src|href)=["\']https?://')

if __name__ == '__main__':
    unittest.main()

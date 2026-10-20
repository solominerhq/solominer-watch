"""Protect the verified font inputs and keep legacy/uncleared assets out of exports."""
from pathlib import Path
import hashlib,json,sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from export_source import source_files

class AssetSources(unittest.TestCase):
    def test_all_font_inputs_are_in_export_with_verified_hashes(self):
        manifest=json.loads((ROOT/'assets/fonts/MANIFEST.json').read_text())
        exported={f.relative_to(ROOT).as_posix() for f in source_files()}
        for name,rec in manifest['fonts'].items():
            path='assets/fonts/'+name
            self.assertIn(path,exported)
            self.assertEqual(hashlib.sha256((ROOT/path).read_bytes()).hexdigest(),rec['sha256'])
        outputs={rec['output'] for rec in manifest['subsets']}
        self.assertEqual(outputs,{f.relative_to(ROOT).as_posix() for f in (ROOT/'src/drivers/displays').glob('inter_*.c')})
        for rec in manifest['subsets']:self.assertIn(rec['source'],manifest['fonts'])
        self.assertIn('assets/fonts/OFL.txt',exported)

    def test_current_art_sources_present_and_unused_images_absent(self):
        exported={f.relative_to(ROOT).as_posix() for f in source_files()}
        for name in ['assets/splash/astronaut.png','assets/celebration/astronaut-poses.png',
                     'assets/celebration/mission_block.png','assets/celebration/mission_found.png']:
            self.assertIn(name,exported)
        for name in ['landing1.c','landing2.c','landing3.c','landing4.c','landing5.c',
                     'landing_frames.h','blockfound_bg.c','block_found_victory.c','charge_bolt.c']:
            self.assertNotIn('src/drivers/displays/'+name,exported)
        self.assertNotIn('src/media/clock_wallpaper.c',exported)

    def test_upstream_snapshots_have_immutable_urls_and_hashes(self):
        records=json.loads((ROOT/'docs/dependency-upstreams.json').read_text())
        self.assertTrue(records)
        for rec in records:
            self.assertEqual(Path(rec['file']).name,rec['file'])
            self.assertRegex(rec['url'],r'^https://codeload\.github\.com/[^/]+/[^/]+/tar\.gz/[0-9a-f]{40}$')
            self.assertRegex(rec['sha256'],r'^[0-9a-f]{64}$')

if __name__=='__main__':unittest.main()

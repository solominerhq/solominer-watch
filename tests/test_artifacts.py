import importlib.util
from pathlib import Path
import tempfile,unittest
spec=importlib.util.spec_from_file_location('artifacts',Path(__file__).resolve().parents[1]/'scripts/firmware_artifacts.py')
artifacts=importlib.util.module_from_spec(spec);spec.loader.exec_module(artifacts)
class ArtifactsTests(unittest.TestCase):
    def test_offsets_and_padding(self):
        image=artifacts.merge_segments([(0,b'boot',8),(16,b'app',4)])
        self.assertEqual(image[:4],b'boot');self.assertEqual(image[16:19],b'app')
        self.assertEqual(image[4:16],b'\xff'*12);self.assertEqual(len(image),4096)
    def test_fail_closed(self):
        for segments in [[(0,b'',4)],[(0,b'12345',4)],[(0,b'1234',4),(2,b'x',4)]]:
            with self.assertRaises(ValueError):artifacts.merge_segments(segments)
    def test_zip_version_without_git(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);(p/'src').mkdir();(p/'src/version.h').write_text('#define CURRENT_VERSION "v3.1.0-dev"')
            self.assertEqual(artifacts.version(p),'v3.1.0-dev')
if __name__=='__main__':unittest.main()

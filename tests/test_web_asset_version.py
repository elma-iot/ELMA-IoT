import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from web_asset_version import source_digest, matches, PREFIX

class WebAssetVersionTests(unittest.TestCase):
    def test_changed_interface_invalidates_portable_bundle(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'web').mkdir()
            app = root / 'web/app.js'
            app.write_text('old interface')
            bundle = root / 'bundle.cpp'
            bundle.write_text(PREFIX + source_digest(root) + '\n')
            self.assertTrue(matches(bundle, source_digest(root)))
            app.write_text('new array controls')
            self.assertFalse(matches(bundle, source_digest(root)))

    def test_legacy_unstamped_bundle_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'bundle.cpp'
            source.write_text('#include "generated_web_assets.h"\n')
            self.assertFalse(matches(source, 'any-digest'))

if __name__ == '__main__':
    unittest.main()

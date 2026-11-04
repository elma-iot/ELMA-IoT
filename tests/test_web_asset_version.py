import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('web_asset_version', Path(__file__).resolve().parents[1] / 'scripts/web_asset_version.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class WebAssetVersionTests(unittest.TestCase):
    def test_text_line_endings_do_not_make_a_portable_checkout_stale(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'web').mkdir()
            text = root / 'web/index.html'
            text.write_bytes(b'<html>\n<body>Board</body>\n</html>\n')
            expected = policy.source_digest(root)
            text.write_bytes(text.read_bytes().replace(b'\n', b'\r\n'))
            self.assertEqual(policy.source_digest(root), expected)
            text.write_bytes(text.read_bytes().replace(b'Board', b'Changed board'))
            self.assertNotEqual(policy.source_digest(root), expected)

    def test_binary_asset_bytes_are_not_normalized(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'web').mkdir()
            binary = root / 'web/image.png'
            binary.write_bytes(b'\x89PNG\r\n\x00')
            expected = policy.source_digest(root)
            binary.write_bytes(b'\x89PNG\n\x00')
            self.assertNotEqual(policy.source_digest(root), expected)

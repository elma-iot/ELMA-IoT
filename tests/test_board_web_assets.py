"""Board variants must omit other boards while retaining every peripheral."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BOARDS = ['esp32-s3-super-mini','esp32-s3-zero','esp32-s3-psram','esp32-spk-n16r8',
          'esp32-s3-devkit-c1','esp32-s3-cam-module','esp32-wrover','esp32-wroom','esp32-mini',
          'wemos-lolin32-mini','esp32-c3','esp32-s2-psram','esp32-c6']

class BoardWebAssetsTests(unittest.TestCase):
    def test_all_board_variants_keep_peripherals(self):
        app=(ROOT/'web/app.js').read_text(encoding='utf-8')
        html=(ROOT/'web/index.html').read_text(encoding='utf-8')
        peripheral_map=re.search(r'const PERIPHERAL_DIAGRAM_ASSET_MAP = (\{.*?^\});',app,re.S|re.M).group(1)
        assets=set(re.findall(r'src: "([^"]+)"',peripheral_map))
        other_options=[v for v in re.findall(r'<option[^>]*value="([^"]+)"',html) if v not in BOARDS]
        with tempfile.TemporaryDirectory() as folder:
            result=subprocess.run(['node',str(ROOT/'scripts/build_board_web.mjs'),str(ROOT),folder],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for index,board in enumerate(BOARDS,1):
                with self.subTest(board=board):
                    variant=pathlib.Path(folder)/'__boards'/str(index)
                    js=(variant/'app.js').read_text(encoding='utf-8')
                    page=(variant/'index.html').read_text(encoding='utf-8')
                    options=re.findall(r'<option[^>]*value="([^"]+)"',page)
                    self.assertEqual([v for v in options if v in BOARDS],[board])
                    self.assertEqual([v for v in options if v not in BOARDS],other_options)
                    for other in BOARDS:
                        if other!=board:self.assertNotIn('"'+other+'"',js)
                    for asset in assets:self.assertIn(asset,js)

if __name__=='__main__': unittest.main()

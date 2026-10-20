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
        app=(ROOT/'web/app.js').read_text(encoding='utf-8',errors='replace')
        html=(ROOT/'web/index.html').read_text(encoding='utf-8')
        peripheral_map=re.search(r'const PERIPHERAL_DIAGRAM_ASSET_MAP = (\{.*?^\});',app,re.S|re.M).group(1)
        assets=set(re.findall(r'src: "([^"]+)"',peripheral_map))
        with tempfile.TemporaryDirectory() as folder:
            result=subprocess.run(['node',str(ROOT/'scripts/build_board_web.mjs'),str(ROOT),folder],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for index,board in enumerate(BOARDS,1):
                with self.subTest(board=board):
                    variant=pathlib.Path(folder)/'__boards'/str(index)
                    js=(variant/'app.js').read_text(encoding='utf-8')
                    page=(variant/'index.html').read_text(encoding='utf-8')
                    selector=re.search(r'<select\b[^>]*id="gpioBoardSelector"[^>]*>(.*?)</select>',page,re.S).group(1)
                    self.assertEqual(re.findall(r'<option[^>]*value="([^"]+)"',selector),[board])
                    for other in BOARDS:
                        if other!=board:self.assertNotIn('"'+other+'"',js)
                    for asset in assets:self.assertIn(asset,js)
            for board_id in (26,27,28):
                variant=pathlib.Path(folder)/'__boards'/str(board_id)
                js=(variant/'app.js').read_text(encoding='utf-8')
                page=(variant/'index.html').read_text(encoding='utf-8')
                audio_selector=re.search(r'<select\b[^>]*id="peripheralAudioProfile"[^>]*>(.*?)</select>',page,re.S).group(1)
                self.assertIn('value="sunton-speaker"',audio_selector)
                if board_id == 28:
                    display_selector=re.search(r'<select\b[^>]*id="peripheralDisplayProfile"[^>]*>(.*?)</select>',page,re.S).group(1)
                    self.assertEqual(re.findall(r'<option[^>]*value="([^"]+)"',display_selector),['viewe-onboard-lcd'])
                    self.assertIn('GT911',display_selector)
                    self.assertNotIn('id="oledPreviewCard"',page)
                    self.assertNotIn('id="wapeModeSection"',page)
                    for field in ('driver','i2cAddress','width','height','sdaPin','sclPin','resetPin'):
                        self.assertNotIn(f'name="oled.{field}"',page)
                    self.assertIn('name="oled.enabled"',page)
                for name in ('camera','microphones','robot','storage-internal','logs'):
                    self.assertNotIn(f'data-tab="{name}"',page)
                    self.assertNotIn(f'id="tab-{name}"',page)
                self.assertNotIn('/api/security/capture',js)
                self.assertNotIn('/api/camera',js)
                self.assertNotIn('/api/logs',js)
                self.assertIn('/bno055-viewer.js',js)
                self.assertTrue((variant/'bno055-viewer.js').is_file())
                self.assertLess(len(js),900000)

if __name__=='__main__': unittest.main()

"""Check the static-to-animation artwork transition without display hardware."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def decode(data):
    palette = [tuple(data[i:i + 4]) for i in range(0, 16, 4)]
    return [palette[(value >> shift) & 3]
            for value in data[16:] for shift in (6, 4, 2, 0)]


class BootArtworkTest(unittest.TestCase):
    def test_static_matches_complete_animation_at_every_pixel(self):
        static_text = (ROOT / 'include/panel_boot_logo.h').read_text()
        data = re.search(r'elmaBootPixels\[\]\s*=\s*\{(.*?)\};', static_text, re.S)[1]
        static = decode(list(map(int, re.findall(r'\d+', data))))
        layers_text = (ROOT / 'include/panel_boot_layers.h').read_text()
        layers = [list(map(int, re.findall(r'\d+', group)))
                  for group in re.findall(r'\{([^{}]+)\}', layers_text)]
        self.assertEqual(len(layers), 26)
        self.assertTrue(all(len(frame) == 4112 for frame in layers))
        self.assertEqual(len(static), 128 * 128)
        complete = [(39, 24, 17, 255)] * (128 * 128)
        for index in (0, 1, 2, 3, 4, 25):
            for pixel, color in enumerate(decode(layers[index])):
                if color[3]:
                    complete[pixel] = color
        self.assertEqual(static, complete,
                         'The apple and every other logo pixel must stay aligned')


if __name__ == '__main__':
    unittest.main()

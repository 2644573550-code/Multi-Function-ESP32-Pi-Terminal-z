#!/usr/bin/env python3
"""Offline tests for PMA conversion and sprite sheet packing."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

script = Path(__file__).resolve().parents[1] / "tools/arkpets/pack_sprites.py"
spec = importlib.util.spec_from_file_location("pack_sprites", script)
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)


class PackSpritesTests(unittest.TestCase):
    def test_fast_pma_conversion_matches_reference(self):
        # Exercise all valid PMA channel/alpha pairs, including alpha zero.
        values = [(channel, channel, channel, alpha)
                  for alpha in range(256) for channel in range(alpha + 1)]
        image = Image.new("RGBA", (len(values), 1))
        image.putdata(values)
        modes = [False]
        try:
            import numpy  # noqa: F401
        except ImportError:
            pass
        else:
            modes.append(True)
        for use_numpy in modes:
            converted = packer.unpremultiply(image, use_numpy=use_numpy)
            actual = list(converted.getdata())
            for (channel, _, _, alpha), pixel in zip(values, actual):
                expected = round(channel * 255 / alpha) if alpha else 0
                self.assertEqual(pixel[3], alpha)
                self.assertLessEqual(abs(pixel[0] - expected), 1)
                self.assertEqual(pixel[:3], (pixel[0],) * 3)

    def test_pack_preserves_sheets_feet_and_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            frames = root / "frames"
            output = root / "packed"
            for action in ("idle", "walk"):
                (frames / action).mkdir(parents=True)
                image = Image.new("RGBA", (2, 2), (0, 0, 0, 0))
                image.putpixel((0, 1), (32, 16, 8, 128))
                image.putpixel((1, 1), (90, 80, 70, 0))
                image.save(frames / action / "000.png")
            (frames / "export.json").write_text(json.dumps({
                "frameWidth": 2, "frameHeight": 2, "columns": 2,
                "actions": {"idle": {"frames": 1}, "walk": {"frames": 1}},
            }))

            info = packer.pack(frames, output)
            self.assertEqual(info["referenceHeight"], 1)
            self.assertEqual(info["actions"]["idle"]["frameFeet"], [2])
            self.assertEqual((output / "idle.png").is_file(), True)
            self.assertEqual(json.loads((output / "animations.json").read_text()), info)
            with Image.open(output / "idle.png") as sheet:
                self.assertEqual(sheet.getpixel((0, 1)), (64, 32, 16, 128))
                self.assertEqual(sheet.getpixel((1, 1)), (0, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()

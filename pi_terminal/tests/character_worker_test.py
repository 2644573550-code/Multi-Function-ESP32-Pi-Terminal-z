#!/usr/bin/env python3
"""Offline safety/behavior tests; never downloads or changes system packages."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

script = Path(__file__).resolve().parents[1]/"tools/arkpets/character_worker.py"
spec = importlib.util.spec_from_file_location("character_worker", script)
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class WorkerTests(unittest.TestCase):
    def test_path_names(self):
        for name in ("../x", "/etc/passwd", "..", ".", "a\\b", "a\0b", ""):
            with self.assertRaises(ValueError): worker.safe_name(name)
        self.assertEqual(worker.safe_name("002_amiya_winter#1"), "002_amiya_winter#1")

    def test_catalog_filters_unsupported_models(self):
        rows = worker.rows({"data": {"a": {"type": "Operator", "name": "A", "style": "BuildingDefault"},
                                    "a_skin": {"type": "Operator", "name": "A", "style": "BuildingSkin", "skinGroupName": "Outfit"},
                                    "dyn_a": {"type": "DynIllust", "name": "A", "skinGroupName": "Animated"},
                                    "b": {"type": "Enemy"}}})
        self.assertEqual([row["id"] for row in rows], ["a", "a_skin", "dyn_a"])
        self.assertEqual([row["character"] for row in rows], ["A", "A", "A"])
        self.assertEqual([row["category"] for row in rows], ["default", "skin", "illustration"])
        self.assertEqual(rows[1]["modelName"], "Outfit")

    def test_install_requires_explicit_consent(self):
        with tempfile.TemporaryDirectory() as root:
            process = subprocess.run([sys.executable, str(script), "install", "--root", root, "--model", "002_amiya"], capture_output=True, text=True)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("授权", process.stdout)
            self.assertFalse((Path(root)/"converter-jars").exists())

    def test_raw_github_reset_falls_back_to_pinned_api_path(self):
        class Response:
            url = "https://api.github.com/repos/isHarryh/Ark-Models/contents/models/amiya%231/skin.png?ref=" + "a"*40
            def __enter__(self): return self
            def __exit__(self, *_): return False
            def read(self, _size):
                if hasattr(self, "used"):
                    self.used = False
                    return b""
                self.used = True
                return b"frame"

        commit = "a"*40
        raw = f"https://raw.githubusercontent.com/isHarryh/Ark-Models/{commit}/models/amiya%231/skin.png"
        with patch.object(worker.urllib.request, "urlopen", side_effect=[urllib.error.URLError("reset"), Response()]) as opened:
            self.assertEqual(worker.download(raw), b"frame")
        fallback = opened.call_args_list[1].args[0]
        self.assertIn("/contents/models/amiya%231/skin.png?ref=" + commit, fallback.full_url)
        self.assertEqual(fallback.get_header("Accept"), "application/vnd.github.raw+json")


if __name__ == "__main__":
    unittest.main()

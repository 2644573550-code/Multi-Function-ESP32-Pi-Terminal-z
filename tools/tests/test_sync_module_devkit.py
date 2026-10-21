import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("sdk_export", Path(__file__).resolve().parents[1] / "sync_module_devkit.py")
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / "esp/component"
        self.target = self.root / "module-devkit/sdk/component"
        self.source.mkdir(parents=True)
        self.manifest = self.root / "module-devkit/sdk/SOURCE-MANIFEST.json"
        (self.source / "source.c").write_text("first\n")

    def run_export(self, flag):
        with patch.object(export, "ROOT", self.root), patch.object(export, "KIT", self.root / "module-devkit"), \
             patch.object(export, "MANIFEST", self.manifest), \
             patch.object(export, "mappings", return_value=[(self.source, self.target)]), \
             patch.object(sys, "argv", ["sync", flag]):
            export.main()

    def test_export_and_update(self):
        self.run_export("--write")
        self.run_export("--check")
        (self.source / "source.c").write_text("second\n")
        with self.assertRaises(SystemExit): self.run_export("--check")
        self.run_export("--write")
        self.assertEqual((self.target / "source.c").read_text(), "second\n")
        self.assertEqual(len(json.loads(self.manifest.read_text())["files"]), 1)

    def test_refuses_developer_edit(self):
        self.run_export("--write")
        (self.target / "source.c").write_text("developer work\n")
        (self.source / "source.c").write_text("new upstream\n")
        with self.assertRaises(SystemExit): self.run_export("--write")
        self.assertEqual((self.target / "source.c").read_text(), "developer work\n")

    def test_does_not_delete_extra_file(self):
        self.run_export("--write")
        (self.target / "other.c").write_text("keep\n")
        with self.assertRaises(SystemExit): self.run_export("--write")
        self.assertTrue((self.target / "other.c").exists())


if __name__ == "__main__":
    unittest.main()

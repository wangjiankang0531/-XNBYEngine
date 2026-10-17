"""Fail-closed tests for the bounded first-child replacement contract."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_zhumadian_child_stage as child
import build_zhumadian_stage_asset as base


class ChildSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.parent = self.directory / "Tile_36_L15_0u.osgb"
        self.next = self.directory / "Tile_36_L16_0uu.osgb"
        self.parent.write_bytes(b"header\0Tile_36_L16_0uu.osgb\0")
        self.next.write_bytes(b"child geometry")
        self.root = base.RootTile(36, 15, self.parent)

    def test_complete_explicit_child_selected(self):
        result = child.select_children([self.root])
        self.assertEqual(result, [base.RootTile(36, 16, self.next)])

    def test_missing_child_rejected(self):
        self.next.unlink()
        with self.assertRaises(base.StageAssetError):
            child.select_children([self.root])

    def test_additional_branch_cannot_be_silently_dropped(self):
        self.parent.write_bytes(self.parent.read_bytes() + b"Tile_36_L16_0uv.osgb\0")
        with self.assertRaises(base.StageAssetError):
            child.select_children([self.root])

    def test_filename_alone_is_not_evidence_of_reference(self):
        self.parent.write_bytes(b"no external child")
        with self.assertRaises(base.StageAssetError):
            child.select_children([self.root])


if __name__ == "__main__":
    unittest.main()

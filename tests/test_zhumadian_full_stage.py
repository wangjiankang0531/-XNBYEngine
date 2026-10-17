import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_zhumadian_full_stage as full
import build_zhumadian_stage_asset as base


class FullSceneTests(unittest.TestCase):
    def test_omitted_root_is_rejected_even_when_other_root_has_faces(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            roots = []
            for i in (1, 2):
                path = folder / f"Tile_{i}.osgb"
                path.write_bytes(f"Tile_+00{i}_+002_L16_0u".encode())
                roots.append(base.RootTile(i, 16, path))
            obj = folder / "roots.obj"
            obj.write_text("g Group_Tile_+001_+002_L16_0u_0\nf 1/1/ 2/2/ 3/3/\n")
            with self.assertRaises(base.StageAssetError):
                full.audit_root_geometry(roots, [obj])
            with obj.open("a") as output:
                output.write("g Group_Tile_+002_+002_L16_0u_0\nf 4/4/ 5/5/ 6/6/\n")
            self.assertEqual(full.audit_root_geometry(roots, [obj]), 2)

    def test_missing_normals_and_obj_to_gltf_uv_conversion(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            texture = folder / "texture.rgb"
            texture.touch()
            obj = folder / "roots.obj"
            obj.with_suffix(".mtl").write_text("newmtl surface\nmap_Kd texture.rgb\n")
            obj.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n"
                           "usemtl surface\nf 1/1/ 2/2/ 3/3/\n")
            output = folder / "geometry.bin"
            views, count, indices, low, high, _ = full.write_geometry([obj], {texture: (8, 16, 128, 64)}, output)
            self.assertEqual((count, indices, low, high), (3, 3, [0, 0, 0], [1, 1, 0]))
            data = output.read_bytes()
            self.assertEqual(struct.unpack_from("<3f", data, views[1]["byteOffset"]), (0, 0, 1))
            uv = struct.unpack_from("<2f", data, views[2]["byteOffset"])
            self.assertEqual(uv, (8 / 8192, 80 / 8192))


if __name__ == "__main__":
    unittest.main()

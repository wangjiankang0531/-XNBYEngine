import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import build_zhumadian_geometry_detail as detail
import build_zhumadian_stage_asset as base


class GeometryDetailTests(unittest.TestCase):
    def test_missing_referenced_child_rejects_whole_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            parent = root/'parent.osgb'
            parent.write_bytes(b'left.osgb right.osgb')
            (root/'left.osgb').write_bytes(b'leaf.osgb')
            with self.assertRaises(base.StageAssetError):
                detail.children_at_depth(parent,1)
            (root/'right.osgb').write_bytes(b'leaf2.osgb')
            self.assertEqual(set(detail.children_at_depth(parent,1)),{root/'left.osgb',root/'right.osgb'})

    def test_interleaved_groups_keep_disjoint_exact_index_spans(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            roots = []
            for tile in (1,2):
                path = root/f'{tile}.osgb'
                path.write_bytes(f'Tile_+00{tile}_+002_L16_0u'.encode())
                roots.append(base.RootTile(tile,16,path))
            obj = root/'roots.obj'
            obj.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\n'+''.join(
                f'g Tile_+00{tile}_+002_L16_0u\nf 1/1 2/2 3/3\n' for tile in (1,2,1)))
            entries,count = detail.partition(roots,[obj])
            by_id = {e['tile_id']:e for e in entries}
            self.assertEqual(count,9)
            self.assertEqual(by_id[1]['spans'],[[0,3],[6,3]])
            self.assertEqual(by_id[2]['spans'],[[3,3]])
            obj.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\ng Tile_+001_+002_L16_0u\nf 1/1 2/2 3/3\n')
            with self.assertRaises(base.StageAssetError):
                detail.partition(roots,[obj])

    def test_child_file_presence_without_faces_does_not_prove_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            children=[]
            for tile in (1,2):
                path=root/f'{tile}.osgb'
                path.write_bytes(f'Tile_+00{tile}_+002_L18_0uuu'.encode())
                children.append(path)
            obj=root/'children.obj'
            obj.write_text('g Tile_+001_+002_L18_0uuu\nf 1/1 2/2 3/3\n')
            with self.assertRaises(base.StageAssetError):
                detail.audit_children(children,obj)
            with obj.open('a') as target:
                target.write('g Tile_+002_+002_L18_0uuu\nf 1/1 2/2 3/3\n')
            detail.audit_children(children,obj)


if __name__=='__main__':
    unittest.main()

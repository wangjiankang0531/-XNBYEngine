import tempfile
import unittest
from pathlib import Path
from convert_zhumadian_native_root import frontier, audit_frontier


class NativeFrontierTests(unittest.TestCase):
    def test_real_branching_and_persistent_terminal_leaves(self):
        catalog = {'root': 'root', 'nodes': [
            {'name': 'root', 'children': ['a', 'b']},
            {'name': 'a', 'children': ['c', 'd', 'e', 'f', 'g']},
            {'name': 'b', 'children': []},
            *[{'name': name, 'children': []} for name in 'cdefg']]}
        self.assertEqual({r['name'] for r in frontier(catalog, 0)}, {'root'})
        self.assertEqual({r['name'] for r in frontier(catalog, 5)}, set('bcdefg'))

    def test_shared_descendant_is_not_drawn_twice(self):
        catalog = {'root': 'root', 'nodes': [
            {'name': 'root', 'children': ['a', 'b']},
            {'name': 'a', 'children': ['leaf']},
            {'name': 'b', 'children': ['leaf']},
            {'name': 'leaf', 'children': []}]}
        self.assertEqual(len(frontier(catalog, 2)), 1)

    def test_native_positive_triangle_loss_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            name = 'Tile_+007_+012_L21_0uuuu21'
            source = folder/'Tile_1_L21_0uuuu21.osgb'
            source.write_bytes(name.encode())
            audit = folder/'counts.tsv'
            audit.write_text(str(source)+'\t1\n')
            obj = folder/'test.obj'
            obj.write_text('')
            with self.assertRaises(AssertionError):
                audit_frontier([source], obj, audit)
            obj.write_text(f'o {name}_Geometry_0\nf 1/1 2/2 3/3\n')
            self.assertEqual(audit_frontier([source], obj, audit), {name: 1})
            audit.write_text(str(source)+'\t0\n')
            obj.write_text('')
            self.assertEqual(audit_frontier([source], obj, audit), {name: 0})


if __name__ == '__main__':
    unittest.main()

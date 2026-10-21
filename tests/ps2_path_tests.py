"""Exercise tile borders, native loader arrays, flags and source corruption."""
import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ps2_paths import area, read_graph, encode_graph, pack_img, export_paths
from ps2_world import Chunk
from traffic_profile import enable_population

spec = importlib.util.spec_from_file_location('independent_native_paths', ROOT / 'tools/validate-sa-paths.py')
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


def source_fixture():
    data = bytearray(512)
    struct.pack_into('<8I', data, 0, 0, 0, len(data), 0, 0, 0, 0, 0)
    struct.pack_into('<I', data, 32, 48)
    struct.pack_into('<5I3i4h2i2B', data, 48, 128, 208, 220, 228, 232, 4, 2, 2, 1, 4, 2, 0, 1, 0, 1, 1)
    for i in range(4):
        struct.pack_into('<7hBB4B', data, 128 + 20 * i, -1, -1, -8 if i % 2 == 0 else 8,
                         800 if i < 2 else 1600, 100, 32766, i, 0 if i < 2 else 16,
                         1 if i < 2 else 2, 1, 0xa0, 0xaf, 0xaa)
    struct.pack_into('<3h2b4B', data, 208, 0, 800, 0, -100, 0, 0xc9, 0xaa, 0, 0xaa)
    struct.pack_into('<4H', data, 220, 1, 0, 3 | 0xc000, 2 | 0xc000)
    data[228:232] = bytes([2, 2, 2, 2])
    struct.pack_into('<2H', data, 232, 0, 0)
    return data


class PathsTests(unittest.TestCase):
    def setUp(self):
        self.source = source_fixture()
        self.graph = read_graph(Chunk(self.source))
        self.payloads, self.addresses, self.nav_addresses = encode_graph(self.graph)

    def native(self):
        regions = validator.parse_archive(pack_img(self.payloads))
        return regions, validator.check_graph(regions)

    def test_cross_tile_graph_and_nav(self):
        regions, report = self.native()
        self.assertEqual(report['VehicleNodes'], 2)
        self.assertEqual(report['PedNodes'], 2)
        self.assertEqual(report['CrossAreaConnections'], 4)
        self.assertEqual(self.addresses[0], (35, 0))
        self.assertEqual(self.addresses[1], (36, 0))
        self.assertEqual(regions[36]['navLinks'][0], 35 << 10)

    def test_ped_lights_and_no_navigation(self):
        regions, _ = self.native()
        r = regions[35]
        self.assertEqual(r['intersections'][1], 3)
        self.assertEqual(r['navLinks'][1], 65535)
        self.assertEqual(r['nodes'][1][9], 16)

    def test_ps2_padding_does_not_become_native_flags(self):
        regions, _ = self.native()
        self.assertEqual(regions[35]['nodes'][0][11], 0x0f1001)
        self.assertEqual(regions[35]['navs'][0][7:], (0x49, 2))

    def test_dead_disabled_water_roadblock_and_speed_mapping(self):
        self.source[128 + 16] = 0xb1  # dead end, off, roadblock, one link
        self.source[148 + 16] = 0xb1
        self.source[128 + 17] = 0xa9  # water and fast
        graph = read_graph(Chunk(self.source))
        payloads, _, _ = encode_graph(graph)
        r = validator.parse_archive(pack_img(payloads))
        self.assertEqual(r[35]['nodes'][0][11], 0x0f21f1)

    def test_source_link_outside_array_rejected(self):
        struct.pack_into('<h', self.source, 128 + 12, 5)
        with self.assertRaisesRegex(ValueError, 'outside graph'):
            read_graph(Chunk(self.source))

    def test_source_invalid_nav_rejected(self):
        struct.pack_into('<H', self.source, 232, 1)
        with self.assertRaisesRegex(ValueError, 'navigation attachment'):
            read_graph(Chunk(self.source))

    def test_source_unresolved_lanes_rejected(self):
        self.source[216] = 0xff
        with self.assertRaisesRegex(ValueError, 'lanes were not resolved'):
            read_graph(Chunk(self.source))

    def test_target_missing_across_region_rejected(self):
        regions, _ = self.native()
        regions[35]['targets'][0] = (63, 0)
        with self.assertRaisesRegex(ValueError, 'nonexistent node'):
            validator.check_graph(regions)

    def test_bad_nav_across_region_rejected(self):
        regions, _ = self.native()
        regions[35]['navLinks'][0] = 0xffff
        with self.assertRaisesRegex(ValueError, 'invalid navigation address'):
            validator.check_graph(regions)

    def test_reserved_dynamic_array_corruption_rejected(self):
        raw = bytearray(self.payloads[35])
        # 2 nodes, one nav, 2 links; reserved addresses follow real links.
        raw[20 + 2 * 28 + 14 + 2 * 4] = 0
        self.payloads[35] = bytes(raw)
        with self.assertRaisesRegex(ValueError, 'reserved dynamic path'):
            validator.parse_archive(pack_img(self.payloads))

    def test_map_bounds_and_nav_capacity(self):
        with self.assertRaisesRegex(ValueError, 'map bounds'):
            area(24000, 0)
        graph = dict(self.graph, navs=self.graph['navs'] * 1025)
        with self.assertRaisesRegex(ValueError, '10-bit'):
            encode_graph(graph)

    def test_population_patch_only_changes_density_operands(self):
        code = (ROOT / 'out/world-stage/data/script/main.scm').read_bytes()
        new = enable_population(code)
        self.assertEqual(len(new), len(code))
        self.assertEqual(struct.unpack_from('<f', new, 654)[0], 1)
        self.assertEqual(struct.unpack_from('<f', new, 661)[0], 1)
        self.assertTrue(all(654 <= i < 658 or 661 <= i < 665 for i, (a, b) in enumerate(zip(code, new)) if a != b))
        self.assertEqual(enable_population(new), new)
        mutant = bytearray(code)
        mutant[651] = 0
        with self.assertRaisesRegex(ValueError, 'density operand changed'):
            enable_population(mutant)

    def test_real_ps2_full_graph_preserved(self):
        rom = ROOT / 'local/originals/lcs-ps2'
        work = ROOT / 'build/path-test-work'
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as tmp:
            assert pathlib.Path(tmp).resolve().is_relative_to(work.resolve())
            output = pathlib.Path(tmp) / 'paths'
            report = export_paths(Chunk.compressed(rom / 'CHK/PS2/GAME.DTZ'), output)
            audit = validator.validate(output / 'paths.img', rom, output)
            self.assertEqual((audit['VehicleNodes'], audit['PedNodes'], audit['NavigationNodes'], audit['DirectedConnections']), (2196, 3323, 2302, 11510))
            self.assertEqual(audit['OriginalPs2ConnectionsCompared'], report['directedConnections'])
            with self.assertRaisesRegex(ValueError, 'exists'):
                export_paths(Chunk.compressed(rom / 'CHK/PS2/GAME.DTZ'), output)


if __name__ == '__main__':
    unittest.main()

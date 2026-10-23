"""Synthetic bounds/serialization checks for the LCS 2DFX exporter.

These tests use authored byte arrays only; they contain no original game data.
"""
import pathlib
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from ps2_effects import effects_by_model, inject_dff, SA_2DFX_PLUGIN, native_light_mapping, trafficlight1_color_by_index
from rwtree import parse


def rw_chunk(ident, ver, body):
    return struct.pack('<3I', ident, len(body), ver) + body


def synthetic_dff(extension=b''):
    version = 0x1803FFFF
    geometry = rw_chunk(15, version,
                        rw_chunk(1, version, b'geometry-struct') +
                        rw_chunk(3, version, extension))
    return rw_chunk(16, version, rw_chunk(26, version, geometry))


def light(sa=None):
    return dict(position=(1.0, 2.0, 3.0), color=(10, 20, 30, 255),
                lodDistance=100.0, pointLightRange=4.0, coronaSize=0.5,
                shadowSize=0.25, coronaTexture='corona', shadowTexture='shadow',
                source=dict(reflection=1, flare=2, shadowIntensity=128, mode=0),
                sa=sa)


def plugin_payload(dff):
    for node in (node for node in parse(dff) for node in walk(node)):
        if node['id'] == SA_2DFX_PLUGIN:
            a, b = node['body']
            return dff[a:b]
    raise AssertionError('plugin not found')


def walk(node):
    yield node
    for child in node.get('kids', ()):
        yield from walk(child)


class FakeChunk:
    def __init__(self, data):
        self.data = bytes(data)

    def u32(self, offset):
        return struct.unpack_from('<I', self.data, offset)[0]


class SourceExtractionTests(unittest.TestCase):
    def test_reads_bounded_model_ranges_and_all_lights(self):
        data = bytearray(1024)
        struct.pack_into('<I', data, 56, 1)       # ResourceImage model count
        struct.pack_into('<I', data, 60, 200)      # model pointer table
        struct.pack_into('<I', data, 84, 6)        # 2DFX count
        struct.pack_into('<I', data, 88, 400)     # 2DFX table
        struct.pack_into('<I', data, 200, 300)    # model 0 pointer
        struct.pack_into('<b', data, 317, 6)      # CBaseModelInfo::num2dFx
        struct.pack_into('<h', data, 324, 0)      # CBaseModelInfo::_2dfxIndex
        for i in range(6):
            offset = 400 + i * 0x40
            struct.pack_into('<3f', data, offset, float(i), 2.0, 3.0)
            data[offset + 16:offset + 20] = bytes((1, 2, 3, 255))
            data[offset + 20] = 0
            struct.pack_into('<4f', data, offset + 24, 100.0, 4.0, 0.5, 0.25)
            data[offset + 40:offset + 45] = bytes((1, 0, 0, 0, 0))
        game = type('GameStub', (), {
            'chunk': FakeChunk(data),
            'models': {0: dict(name='trafficlight1')},
        })()
        effects = effects_by_model(game)
        self.assertEqual(len(effects[0]), 6)
        self.assertEqual([row['position'][0] for row in effects[0]],
                         [0.0, 1.0, 2.0, 3.0, 4.0, 5.0])


class PluginTests(unittest.TestCase):
    def test_native_night_gate_and_owned_signal_colors(self):
        effect = light()
        effect['source'].update(mode=1, flags=1)
        self.assertEqual(native_light_mapping(effect), dict(flags=0x41, flashiness=0))
        effect['source']['mode'] = 0
        self.assertEqual(native_light_mapping(effect), dict(flags=0x61, flashiness=0))
        signals = []
        for y in (-1.0, 1.0):
            for z in (1.0, 2.0, 3.0):
                row = light()
                row.update(position=(0.0, y, z), modelName='trafficlight1', color=(255,255,255,200))
                row['source']['mode'] = 12
                signals.append(row)
        translated = trafficlight1_color_by_index(signals)
        self.assertEqual([row['position'] for row in translated], [row['position'] for row in signals])
        self.assertEqual([row['color'][:3] for row in translated[:3]], [(0,255,0),(255,128,0),(255,0,0)])
        self.assertEqual(native_light_mapping(translated[0]), dict(flags=0, flashiness=0))

    def test_writes_sa_final_light_record_and_keeps_other_extensions(self):
        sa = dict(flags=0x1234, flashiness=0, lightDirection=(0, 0, 100))
        other = rw_chunk(0x12345678, 0x1803FFFF, b'keep-me')
        source = synthetic_dff(other)
        changed = inject_dff(source, [light(sa)])
        payload = plugin_payload(changed)
        count, = struct.unpack_from('<I', payload)
        self.assertEqual(count, 1)
        x, y, z, effect_type, attr_size = struct.unpack_from('<3fII', payload, 4)
        position = (x, y, z)
        self.assertEqual(position, (1.0, 2.0, 3.0))
        self.assertEqual((effect_type, attr_size), (0, 80))
        self.assertEqual(len(payload), 4 + 20 + 80)
        self.assertIn(other, changed)
        self.assertEqual(inject_dff(changed, [light(sa)]), changed)

    def test_refuses_to_replace_an_existing_sa_plugin(self):
        existing = rw_chunk(SA_2DFX_PLUGIN, 0x1803FFFF, b'authored')
        source = synthetic_dff(existing)
        original = bytes(source)
        with self.assertRaisesRegex(ValueError, 'preserving it'):
            inject_dff(source, [light(dict(flags=0, flashiness=0))])
        self.assertEqual(source, original)

    def test_requires_explicit_temporal_translation(self):
        with self.assertRaisesRegex(ValueError, 'temporal flags/flashiness'):
            inject_dff(synthetic_dff(), [light()])


if __name__ == '__main__':
    unittest.main()

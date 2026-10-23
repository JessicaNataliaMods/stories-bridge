"""Archive mutation contracts using authored bytes, without game data."""
from pathlib import Path
import os
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import map_effects


class ArchiveContracts(unittest.TestCase):
    def test_rewrite_preserves_other_assets_and_hardlinked_rollback(self):
        dff = struct.pack('<3I', 16, 3, 0x1803ffff)+b'abc'
        texture = b'authored texture payload'
        original = (struct.pack('<4sI', b'VER2', 2)
                    +struct.pack('<IHH24s', 1, 1, 0, b'prop.dff')
                    +struct.pack('<IHH24s', 2, 1, 0, b'prop.txd'))
        original += bytes(2048-len(original))+dff+bytes(2048-len(dff))
        original += texture+bytes(2048-len(texture))
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1], prefix='.map-effects-test-') as directory:
            target = Path(directory)/'world.img'
            rollback = Path(directory)/'rollback.img'
            target.write_bytes(original)
            os.link(target, rollback)
            changed_dff = dff + b'authored effect payload'
            with patch.object(map_effects, 'inject_dff', return_value=changed_dff):
                changes = map_effects.rewrite_archive(target, {'prop.dff': [object()]})
            self.assertEqual(rollback.read_bytes(), original)
            self.assertEqual(len(changes), 1)
            with target.open('rb') as stream:
                rows = map_effects.entries(stream)
                sector, size, name = rows[1]
                self.assertEqual(name, 'prop.txd')
                stream.seek(sector*2048)
                self.assertEqual(stream.read(size*2048), texture+bytes(2048-len(texture)))

    def test_archive_cannot_reference_directory_as_payload(self):
        with tempfile.TemporaryFile() as stream:
            stream.write(struct.pack('<4sI', b'VER2', 1)+struct.pack('<IHH24s', 0, 1, 0, b'prop.dff')+bytes(2048))
            stream.seek(0)
            with self.assertRaises(ValueError):
                map_effects.entries(stream)

    def test_complete_geometry_replacement_is_bounded_and_replayable(self):
        original_dff = struct.pack('<3I', 16, 3, 0x1803ffff)+b'old'
        replacement = struct.pack('<3I', 16, 7, 0x1803ffff)+b'newmesh'
        archive = struct.pack('<4sI', b'VER2', 1)+struct.pack('<IHH24s', 1, 1, 0, b'prop.dff')
        archive += bytes(2048-len(archive))+original_dff+bytes(2048-len(original_dff))
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)/'world.img'
            target.write_bytes(archive)
            self.assertEqual(len(map_effects.rewrite_archive(target, {}, {'prop.dff':replacement})), 1)
            self.assertEqual(map_effects.rewrite_archive(target, {}, {'prop.dff':replacement}), [])
            with target.open('rb') as stream:
                sector, size, _ = map_effects.entries(stream)[0]
                stream.seek(sector*2048)
                self.assertEqual(map_effects.dff_bytes(stream.read(size*2048)), replacement)


if __name__ == '__main__':
    unittest.main()

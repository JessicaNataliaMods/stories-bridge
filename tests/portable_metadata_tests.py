"""Author-created metadata only; no game fixtures or private inputs."""
import json
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_metadata import make_portable

spec = importlib.util.spec_from_file_location('package_validator', Path(__file__).resolve().parents[1] / 'tools/validate-modloader-map.py')
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)
spec = importlib.util.spec_from_file_location('map_update', Path(__file__).resolve().parents[1] / 'tools/prepare-map-feature-update.py')
map_update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(map_update)
spec = importlib.util.spec_from_file_location('metadata_migration', Path(__file__).resolve().parents[1] / 'tools/migrate-package-metadata.py')
metadata_migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata_migration)


def archive(entries):
    """Small author-created IMG, with no extracted models or game data."""
    header = bytearray(b'VER2' + struct.pack('<I', len(entries)))
    sector = (8 + 32 * len(entries) + 2047) // 2048
    payloads = bytearray()
    for name, payload in entries:
        size = (len(payload) + 2047) // 2048
        header.extend(struct.pack('<IHH24s', sector, size, 0, name.encode()))
        payloads.extend(payload.ljust(size * 2048, b'\0'))
        sector += size
    return bytes(header).ljust((len(header) + 2047) // 2048 * 2048, b'\0') + payloads


def fixture(package):
    def put(relative, value):
        path = package / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value if isinstance(value, bytes) else value.encode())
    for name in ('LcsSaBridge.asi', 'LcsSaBridge.ini', 'LEIA.txt', 'data/script/main.scm', 'data/water.dat'):
        put(name, 'authored structural fixture')
    put('metadata/map-preview.json', json.dumps(dict(mode='WorldPreview', modelAliases={}, textureDictionaryAliases={})))
    put('metadata/native-collisions.json', '{}')
    put('data/lcs/world.ide', 'objs\n10000, custom, custom, 1, 100, 0\nend\n')
    put('data/lcs/model-bindings.tsv', '1\t10000\toriginal\tcustom\n')
    put('data/lcs/door-bindings.tsv', '')
    put('data/lcs/render-ranges.tsv', '10000\t0\t0\t-1\t1\t1\n')
    put('data/lcs/island-zones.tsv', '0\t-10\t-10\t-10\t10\t10\t10\n')
    put('data/lcs/world0.ipl', 'inst\n10000, custom, 0, 0, 0, 0, 0, 0, 0, 1, -1\nend\n')
    put('data/gta.dat', 'IMG models/lcs/world.img\nIMG models/lcs/paths.img\nIDE data/lcs/world.ide\nCOLFILE 0 models/lcs/world.col\nCOLFILE 0 models/lcs/native-objects.col\nIPL data/lcs/world0.ipl\n')
    put('models/lcs/world.img', archive([('custom.dff', b'customized mesh payload'), ('custom.txd', b'customized texture payload')]))
    # 64 empty native path areas are valid structure, with no original graph.
    put('models/lcs/paths.img', archive([(f'nodes{i}.dat', bytes(20)) for i in range(64)]))
    for relative, name in (('models/lcs/world.col', 'custom'), ('models/lcs/native-objects.col', 'retained')):
        put(relative, (b'COL2' + struct.pack('<I', 96) + name.encode() + b'\0').ljust(104, b'\0'))
    put('models/radar/radar00.txd', b'authored texture placeholder')


class PortableMetadataTests(unittest.TestCase):
    def test_installed_migration_backs_up_json_and_refreshes_only_metadata(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            directory = Path(temporary)
            package = directory / 'package'
            package.mkdir()
            fixture(package)
            storage = package / 'data/lcs/animations/storage.json'
            storage.parent.mkdir(parents=True)
            storage.write_text(json.dumps(dict(source='Z:\\PrivateWorkspace\\source.ifp', nativeBlocks=2)))
            campaign = package / 'metadata/campaign.json'
            campaign.write_text(json.dumps(dict(version=127, nativeStartupValidated=True,
                                                animationStorage=dict(sha256='stale'))))
            manifest = package / 'metadata/package-files.json'
            manifest.write_text(json.dumps([dict(relative=name, sha256='stale', bytes=0)
                                           for name in ('data/lcs/animations/storage.json', 'metadata/campaign.json')]))
            original_storage = storage.read_bytes()
            original_asi = (package / 'LcsSaBridge.asi').read_bytes()
            backup = directory / 'rollback'
            preview = metadata_migration.migrate(package, backup)
            self.assertFalse(preview['applied'])
            self.assertFalse(backup.exists())
            self.assertEqual(original_storage, storage.read_bytes())
            result = metadata_migration.migrate(package, backup, apply=True)
            self.assertTrue(result['applied'])
            self.assertEqual((backup / 'data/lcs/animations/storage.json').read_bytes(), original_storage)
            self.assertEqual(json.loads(storage.read_text()), dict(nativeBlocks=2))
            self.assertTrue(json.loads(campaign.read_text())['nativeStartupValidated'])
            self.assertEqual(original_asi, (package / 'LcsSaBridge.asi').read_bytes())
            for row in json.loads(manifest.read_text()):
                self.assertEqual(row['sha256'], hashlib.sha256((package / row['relative']).read_bytes()).hexdigest())
            self.assertFalse(metadata_migration.migrate(package, directory / 'unused')['changedFiles'])

    def test_map_update_preserves_custom_weapon_bytes_and_rejects_missing_slot(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            package = Path(temporary)
            fixture(package)
            (package / 'weapons').mkdir()
            for suffix in ('.dff', '.txd'):
                (package / 'weapons' / ('custom' + suffix)).write_bytes(b'user-customized-content')
            (package / 'data/weapon.dat').write_text('user-customized-configuration')
            weapons = dict(slots=[dict(nativeName='custom')],
                           files=[dict(relative='weapons/custom'+suffix, sha256='old-conversion-hash') for suffix in ('.dff', '.txd')],
                           weaponDat=dict(relative='data/weapon.dat', sha256='old-conversion-hash'))
            (package / 'metadata/weapon-visuals.json').write_text(json.dumps(weapons))
            for name in ('map-damage', 'map-effects'):
                (package / 'metadata' / (name + '.json')).write_text('{}')
            campaign = dict(version=126, originalResources=[], preservedApprovedResources=[],
                            weaponVisuals=dict(manifest='metadata/weapon-visuals.json', sha256='old'),
                            mapDamage=dict(manifest='metadata/map-damage.json', sha256='old'),
                            mapEffects=dict(manifest='metadata/map-effects.json', sha256='old'))
            (package / 'metadata/campaign.json').write_text(json.dumps(campaign))
            proof = map_update.reconcile_resource_records(package)
            self.assertFalse(proof['sourceProvenanceValidated'])
            self.assertEqual((package / 'weapons/custom.dff').read_bytes(), b'user-customized-content')
            self.assertEqual((package / 'data/weapon.dat').read_text(), 'user-customized-configuration')
            (package / 'weapons/custom.txd').unlink()
            with self.assertRaises(ValueError):
                map_update.reconcile_resource_records(package)

    def test_customized_content_accepted_without_original_hashes(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            package = Path(temporary)
            fixture(package)
            (package / 'metadata/legacy-provenance.json').write_text(json.dumps({
                'sourceManifest': 'Z:\\PrivateWorkspace\\old-machine\\manifest.json',
                'sourceManifestSha256': 'obsolete-reference-hash',
            }))
            first = validator.validate(package)
            self.assertEqual(first['validationScope'], 'structure')
            self.assertNotIn('sourceManifest', json.loads((package / 'metadata/legacy-provenance.json').read_text()))
            before = json.loads((package / 'metadata/package-files.json').read_text())
            (package / 'models/lcs/world.img').write_bytes(archive([
                ('custom.dff', b'a different user mesh with different length'),
                ('custom.txd', b'different user textures'),
            ]))
            second = validator.validate(package)
            self.assertTrue(second['valid'])
            after = json.loads((package / 'metadata/package-files.json').read_text())
            self.assertNotEqual(next(row['sha256'] for row in before if row['relative'].endswith('world.img')),
                                next(row['sha256'] for row in after if row['relative'].endswith('world.img')))

    def test_corrupt_archive_and_undefined_model_rejected(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            package = Path(temporary)
            fixture(package)
            (package / 'models/lcs/world.img').write_bytes(b'VER2' + struct.pack('<I', 9000))
            with self.assertRaises(ValueError):
                validator.validate(package)
            fixture(package)
            (package / 'data/lcs/world0.ipl').write_text('inst\n10001, missing, 0, 0, 0, 0, 0, 0, 0, 1, -1\nend\n')
            with self.assertRaises(ValueError):
                validator.validate(package)

    def test_nested_external_paths_removed_internal_paths_relocated(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            package = Path(temporary)
            metadata = package / 'metadata'
            metadata.mkdir()
            source = metadata / 'custom.json'
            source.write_text(json.dumps({
                'sourceManifest': str(metadata / 'sources/custom.json'),
                'externalSource': 'Z:\\Personal\\secret-workspace\\manifest.json',
                'nested': [{'source': '/home/example/private/file', 'relative': 'weapons/custom.dff'}],
                'sha256': 'informational', 'vertices': 999,
            }), encoding='utf-8')
            storage = package / 'data/lcs/animations/storage.json'
            storage.parent.mkdir(parents=True)
            storage.write_text(json.dumps(dict(source='Z:\\PrivateWorkspace\\animations.ifp', nativeBlocks=2)))
            make_portable(package)
            self.assertEqual(json.loads(storage.read_text()), dict(nativeBlocks=2))
            cleaned = json.loads(source.read_text())
            self.assertEqual(cleaned['sourceManifest'], 'metadata/sources/custom.json')
            self.assertNotIn('externalSource', cleaned)
            self.assertEqual(cleaned['nested'], [{'relative': 'weapons/custom.dff'}])
            self.assertEqual(cleaned['sha256'], 'informational')
            self.assertEqual(cleaned['vertices'], 999)
            first = source.read_bytes()
            make_portable(package)
            self.assertEqual(first, source.read_bytes())

    def test_invalid_json_fails_instead_of_silently_approving(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            metadata = Path(temporary) / 'metadata'
            metadata.mkdir()
            (metadata / 'bad.json').write_text('{', encoding='utf-8')
            with self.assertRaises(json.JSONDecodeError):
                make_portable(Path(temporary))


if __name__ == '__main__':
    unittest.main()

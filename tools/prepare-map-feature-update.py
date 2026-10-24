"""Stage map fixes over the exact current local package, preserving custom assets.

Unlike a campaign refresh, this never regenerates weapons, HUD, audio, paths,
scripts or vehicles. The normal startup and package validators still apply.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
from pathlib import PurePosixPath, PureWindowsPath
import shutil
import tempfile

import map_effects
import map_damage
from package_metadata import make_portable

ROOT = Path(__file__).resolve().parents[1]


def reconcile_resource_records(package):
    """Refresh package-local hashes from the current files without provenance claims.

    Map feature updates do not rewrite weapon or ped assets. Their current bytes
    are inventoried so package integrity checks describe this staged package;
    this does not prove how those assets were produced.
    """
    package = Path(package).resolve()
    campaign_path = package/'metadata/campaign.json'
    campaign = json.loads(campaign_path.read_text(encoding='utf-8'))

    def package_file(relative, required=True):
        if not isinstance(relative, str) or not relative.strip():
            raise ValueError('Package resource path is missing')
        normalized = relative.replace('\\', '/')
        posix = PurePosixPath(normalized)
        windows = PureWindowsPath(normalized)
        if posix.is_absolute() or windows.is_absolute() or windows.drive or '..' in posix.parts or \
                any(':' in part for part in posix.parts) or not posix.parts:
            raise ValueError('Package resource path is not relative: '+relative)
        normalized = posix.as_posix()
        target = package.joinpath(*posix.parts)
        cursor = package
        for part in posix.parts:
            cursor = cursor/part
            if cursor.is_symlink() or cursor.is_junction():
                raise ValueError('Linked package resource is not allowed: '+normalized)
        if not target.exists():
            if required:
                raise ValueError('Missing package resource: '+normalized)
            return normalized, None
        resolved = target.resolve(strict=True)
        if package not in resolved.parents or not resolved.is_file():
            raise ValueError('Package resource is outside the package or not a file: '+normalized)
        return normalized, resolved

    obsolete_weapon_rows = []
    refreshed_files = []
    visual_pin = campaign.get('weaponVisuals')
    if campaign.get('version', 0) >= 94 and not visual_pin:
        raise ValueError('Missing weapon visual inventory for the declared campaign version')
    if visual_pin:
        if not isinstance(visual_pin, dict):
            raise ValueError('Invalid weapon visual manifest reference')
        manifest_rel, weapon_path = package_file(visual_pin.get('manifest'))
        weapons = json.loads(weapon_path.read_text(encoding='utf-8'))
        if not isinstance(weapons, dict) or not isinstance(weapons.get('slots'), list) or not weapons['slots'] or not isinstance(weapons.get('files'), list):
            raise ValueError('Invalid weapon visual inventory')

        expected = set()
        slot_names = set()
        for slot in weapons['slots']:
            native_name = slot.get('nativeName') if isinstance(slot, dict) else None
            if not isinstance(native_name, str) or not native_name or '/' in native_name or '\\' in native_name or ':' in native_name or native_name in ('.', '..'):
                raise ValueError('Invalid declared weapon slot target')
            if native_name.lower() in slot_names:
                raise ValueError('Duplicate declared weapon slot target: '+native_name)
            slot_names.add(native_name.lower())
            expected.update(('weapons/'+native_name+suffix).lower() for suffix in ('.dff', '.txd'))

        kept_files = []
        seen_files = set()
        declared_files = set()
        for row in weapons['files']:
            if not isinstance(row, dict):
                raise ValueError('Invalid weapon file inventory row')
            relative, target = package_file(row.get('relative'), required=False)
            path_parts = PurePosixPath(relative).parts
            if not path_parts or path_parts[0].lower() != 'weapons' or PurePosixPath(relative).suffix.lower() not in ('.dff', '.txd'):
                raise ValueError('Invalid weapon asset inventory path: '+relative)
            key = relative.lower()
            if key in seen_files:
                raise ValueError('Duplicate weapon file inventory row: '+relative)
            seen_files.add(key)
            if key in expected:
                declared_files.add(key)
            if target is None:
                if key in expected:
                    raise ValueError('Missing file for a declared weapon slot: '+relative)
                obsolete_weapon_rows.append(relative)
                continue
            row['relative'] = relative
            actual = map_effects.digest(target)
            if row.get('sha256') != actual:
                refreshed_files.append(relative)
            row['sha256'] = actual
            kept_files.append(row)
        missing_slots = expected-declared_files
        if missing_slots:
            raise ValueError('Declared weapon slots lack file inventory rows: '+', '.join(sorted(missing_slots)))
        weapons['files'] = kept_files

        weapon_data = weapons.get('weaponDat')
        if campaign.get('version', 0) >= 94 and not isinstance(weapon_data, dict):
            raise ValueError('Missing converted weapon.dat inventory reference')
        if weapon_data is not None:
            if not isinstance(weapon_data, dict):
                raise ValueError('Invalid converted weapon.dat inventory reference')
            data_rel, data_path = package_file(weapon_data.get('relative'))
            if campaign.get('version', 0) >= 94 and data_rel.lower() != 'data/weapon.dat':
                raise ValueError('Converted weapon.dat must reference data/weapon.dat')
            weapon_data['relative'] = data_rel
            actual = map_effects.digest(data_path)
            if weapon_data.get('sha256') != actual:
                refreshed_files.append(data_rel)
            weapon_data['sha256'] = actual

        weapon_path.write_text(json.dumps(weapons, indent=2)+'\n', encoding='utf-8')
        visual_pin['manifest'] = manifest_rel
        visual_pin['sha256'] = map_effects.digest(weapon_path)
        visual_pin['slots'] = len(weapons['slots'])

    # Drop only missing weapon records explicitly declared obsolete by the
    # weapon manifest. Every other campaign resource must still be present.
    obsolete_keys = {name.lower() for name in obsolete_weapon_rows}
    refreshed_resources = []
    for section in ('originalResources', 'preservedApprovedResources'):
        rows = campaign.get(section)
        if not isinstance(rows, list):
            raise ValueError('Missing campaign resource inventory: '+section)
        kept_rows = []
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError('Invalid campaign resource inventory row: '+section)
            relative, target = package_file(row.get('relative'), required=False)
            if target is None:
                if relative.lower() in obsolete_keys:
                    continue
                raise ValueError('Missing declared campaign resource: '+relative)
            row['relative'] = relative
            actual = map_effects.digest(target)
            if not isinstance(row.get('sha256'), str) or row['sha256'].lower() != actual:
                refreshed_resources.append(relative)
            row['sha256'] = actual
            kept_rows.append(row)
        campaign[section] = kept_rows

    for key in ('mapDamage', 'mapEffects'):
        if not isinstance(campaign.get(key), dict) or not isinstance(campaign[key].get('manifest'), str):
            raise ValueError('Missing required map feature manifest reference: '+key)

    def refresh_manifest_pins(value):
        if isinstance(value, dict):
            relative = value.get('manifest')
            if 'manifest' in value and 'sha256' in value:
                if not isinstance(relative, str):
                    raise ValueError('Invalid package manifest reference')
                normalized, target = package_file(relative)
                actual = map_effects.digest(target)
                if value.get('sha256') != actual:
                    refreshed_resources.append(normalized)
                value['manifest'] = normalized
                value['sha256'] = actual
            for item in value.values():
                refresh_manifest_pins(item)
        elif isinstance(value, list):
            for item in value:
                refresh_manifest_pins(item)

    refresh_manifest_pins(campaign)
    previous = campaign.get('preservedAssetBookkeeping', {})
    if not isinstance(previous, dict):
        raise ValueError('Invalid prior preserved asset bookkeeping')
    old_absent = previous.get('absentObsoleteWeaponEntries', [])
    old_visuals = previous.get('explicitWeaponVisuals', [])
    if not isinstance(old_absent, list) or not all(isinstance(item, str) for item in old_absent) or \
            not isinstance(old_visuals, list) or not all(isinstance(item, str) for item in old_visuals):
        raise ValueError('Invalid prior preserved asset bookkeeping lists')
    campaign['preservedAssetBookkeeping'] = dict(
        currentFileHashesRefreshed=sorted(set(refreshed_resources+refreshed_files)),
        absentObsoleteWeaponEntries=sorted(set(obsolete_weapon_rows) | set(old_absent)),
        explicitWeaponVisuals=sorted(set(old_visuals)),
        sourceProvenanceValidated=False,
        customWeaponAndPedAssetsWritten=False)
    campaign_path.write_text(json.dumps(campaign, indent=2)+'\n')
    return campaign['preservedAssetBookkeeping']


def prepare(base, output, original, rom, names, asi, version, asset_cache=None, map_source=None, converter=None):
    base, output = base.resolve(), output.resolve()
    if output.exists() or base == output or base in output.parents or output in base.parents:
        raise ValueError('Choose a new separate private staging directory')
    campaign = json.loads((base/'metadata/campaign.json').read_text())
    if campaign['mode'] != 'Campaign' or campaign['version'] >= version:
        raise ValueError('Invalid campaign source/version')
    output.mkdir(parents=True)
    preserved = 0
    for source in base.rglob('*'):
        relative = source.relative_to(base)
        if source.is_symlink() or source.is_junction():
            raise ValueError('Package source must contain no reparse points')
        if relative.parts[0].lower() == 'audio' or source.suffix.lower() == '.log':
            continue
        target = output/relative
        if source.is_dir():
            target.mkdir(exist_ok=True)
        elif relative.parts[0] == 'metadata' or source.suffix.lower() in ('.asi', '.ini'):
            shutil.copy2(source, target)
        else:
            cached = asset_cache/relative if asset_cache else None
            if source.drive.lower() == target.drive.lower():
                os.link(source, target)
                preserved += 1
            elif cached and cached.is_file() and cached.stat().st_size == source.stat().st_size and map_effects.digest(cached) == map_effects.digest(source):
                os.link(cached, target)
                preserved += 1
            else:
                shutil.copy2(source, target)
    shutil.copy2(asi, output/'LcsSaBridge.asi')
    campaign['version'] = version
    campaign['nativeStartupValidated'] = False
    campaign.pop('startupValidationSha256', None)
    (output/'metadata/campaign.json').write_text(json.dumps(campaign, indent=2))
    spec = importlib.util.spec_from_file_location('map_objects', ROOT/'tools/mount-campaign-garage-doors.py')
    objects = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(objects)
    preview = json.loads((output/'metadata/map-preview.json').read_text())
    game = Path(preview['gameDirectory'])
    if not map_source or not converter:
        raise ValueError('Map feature update requires source damage geometry and its converter')
    source_game = map_effects.Game(rom, names)
    model_records = {}
    for line in (output/'data/lcs/model-bindings.tsv').read_text().splitlines():
        source_id, runtime_id, source_name, alias = line.split('\t')
        model_records[int(source_id)] = dict(name=alias)
    source_rows = objects.read_object_data(original/'DATA/OBJECT.DAT', 11)
    fragment_sources = {name for name, row in source_rows.items() if int(row[8]) in (3, 50, 60, 70, 80, 100)}
    with tempfile.TemporaryDirectory(prefix='.map-damage-', dir=output) as temporary:
        assets = Path(temporary)
        selected = {row['name'].lower()+'.dff' for model_id, row in model_records.items()
                    if map_damage._source_damage(source_game.models[model_id]) is None or
                    source_game.models[model_id].get('name', '').lower() in fragment_sources}
        with (output/'models/lcs/world.img').open('rb') as archive:
            for sector, size, name in map_effects.entries(archive):
                if name.lower() in selected:
                    archive.seek(sector*2048)
                    (assets/name).write_bytes(map_effects.dff_bytes(archive.read(size*2048)))
        damage = map_damage.export_damage(source_game, model_records, assets,
            map_source/'streamed-source', converter, map_source/'intermediate/map-lighting.psl',
            output/'metadata/map-damage.json', breakable_names=fragment_sources)
        replacements = {name.lower()+'.dff':(assets/(name+'.dff')).read_bytes() for name in damage['replacementAssets']}
        map_effects.rewrite_archive(output/'models/lcs/world.img', {}, replacements)
    physics = objects.mount(output, original, game/'data/object.dat',
        damaged_atomic_names=damage['damageAtomicBySourceModel'],
        breakable_names=damage['breakableBySourceModel'],
        # SA's generic break effect shatters a mesh per triangle, which is not
        # what LCS does. Only the traffic light uses it, and there the runtime
        # replaces the shatter with the original LCS pieces.
        breakable_effects={name:200 for name in damage['breakableBySourceModel'] if name == 'trafficlight1'})
    effects = map_effects.mount(output, rom, names)
    campaign = json.loads((output/'metadata/campaign.json').read_text())
    campaign['mapDamage'] = dict(manifest='metadata/map-damage.json', sha256=map_effects.digest(output/'metadata/map-damage.json'))
    campaign['mapEffects'] = dict(manifest='metadata/map-effects.json', sha256=map_effects.digest(output/'metadata/map-effects.json'))
    changed = {'LcsSaBridge.asi', 'models/lcs/world.img', 'data/lcs/world.ide',
               'data/object.dat', 'data/lcs/door-bindings.tsv', 'metadata/garage-doors.json'}
    for row in campaign['originalResources'] + campaign['preservedApprovedResources']:
        if row['relative'] in changed:
            row['sha256'] = map_effects.digest(output/row['relative'])
    campaign['mapFeatureUpdate'] = dict(base=str(base), version=version,
        assetsRegenerated=['map effects', 'map object physics'],
        gameExecuted=False, visualVerified=False)
    (output/'metadata/campaign.json').write_text(json.dumps(campaign, indent=2)+'\n')
    preview['version'] = str(version)
    (output/'metadata/map-preview.json').write_text(json.dumps(preview, indent=2)+'\n')
    make_portable(output)
    bookkeeping = reconcile_resource_records(output)
    return dict(stage=str(output), version=version, sharedAssets=preserved,
                physics=physics, effects=effects, damage=damage, gameExecuted=False,
                preservedAssetBookkeeping=bookkeeping)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('base', 'output', 'original', 'rom', 'names', 'asi'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--version', type=int, required=True)
    parser.add_argument('--asset-cache', type=Path, help='Existing local stage; only byte-identical assets may be shared')
    parser.add_argument('--map-source', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(**vars(args)), indent=2))

"""Stage map fixes over the exact current local package, preserving custom assets.

Unlike a campaign refresh, this never regenerates weapons, HUD, audio, paths,
scripts or vehicles. The normal startup and package validators still apply.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile

import map_effects
import map_damage
from gameplay_ped_profile import validate as validate_peds
from native_collisions import retained_definitions
import weapon_slots

ROOT = Path(__file__).resolve().parents[1]


def reconcile_resource_records(package):
    """Repair stale bookkeeping only after independent asset validation.

    Never repin a modified weapon or skin just because it exists. Ped files
    must reproduce their source conversion; weapon entries may only lose
    absent files belonging to obsolete target names, followed by full package
    validation of the remaining weapon conversion.
    """
    campaign_path = package/'metadata/campaign.json'
    campaign = json.loads(campaign_path.read_text())
    preview = json.loads((package/'metadata/map-preview.json').read_text())
    proof = validate_peds(package, Path(preview['gameDirectory']))
    if not proof or proof.get('valid') is not True:
        raise ValueError('Missing independent ped conversion proof')
    refreshed = []
    for row in campaign['originalResources']+campaign['preservedApprovedResources']:
        if row['relative'].startswith('models/lcs/peds/'):
            actual = map_effects.digest(package/row['relative'])
            if row['sha256'].lower() != actual:
                row['sha256'] = actual
                refreshed.append(row['relative'])
    weapon_path = package/'metadata/weapon-visuals.json'
    weapons = json.loads(weapon_path.read_text())
    expected = {'weapons/'+slot['nativeName']+suffix for slot in weapons['slots'] for suffix in ('.dff','.txd')}
    actual = {row['relative'] for row in weapons['files']}
    obsolete = actual-expected
    if expected-actual or any((package/name).exists() for name in obsolete):
        raise ValueError('Weapon inventory requires an asset change; bookkeeping repair refused')
    source_manifest = Path(weapons['sourceManifest'])
    if map_effects.digest(source_manifest) != weapons['sourceManifestSha256']:
        raise ValueError('Weapon conversion source changed')
    converted = json.loads(source_manifest.read_text())
    by_name = {row['originalName'].lower():row for row in converted['converted']}
    visual_choices = []
    for slot in weapons['slots']:
        visual = weapon_slots.VISUAL_SOURCE_OVERRIDES.get(slot['selectedOriginal'].lower())
        if not visual:
            continue
        source_row = by_name[visual]
        for suffix, key in (('.dff','dffSHA256'),('.txd','txdSHA256')):
            if map_effects.digest(package/('weapons/'+slot['nativeName']+suffix)) != source_row[key]:
                raise ValueError('Preserved weapon visual differs from its explicit source')
        if slot.get('visualOriginal') != visual:
            slot['visualOriginal'] = visual
            visual_choices.append(slot['nativeName'])
    game = Path(preview['gameDirectory'])
    sa_models = {int(fields[0]):fields[1].lower() for section, fields in retained_definitions(game) if section == 'weap'}
    regenerated = weapon_slots.convert_weapon_dat((game/'data/weapon.dat').read_text(encoding='latin1'),
        (package/'metadata/lcs-source/DATA/WEAPON.DAT.bin').read_text(encoding='latin1'),
        {row['originalModelId']:row['originalName'].lower() for row in converted['converted']},
        sa_models, animations=campaign['version']>=95).encode('latin1')
    if (package/'data/weapon.dat').read_bytes() != regenerated:
        raise ValueError('Current weapon.dat does not reproduce its converter inputs')
    weapon_data_sha = map_effects.digest(package/'data/weapon.dat')
    data_pin_changed = weapons['weaponDat']['sha256'] != weapon_data_sha
    if obsolete or data_pin_changed or visual_choices:
        weapons['files'] = [row for row in weapons['files'] if row['relative'] in expected]
        weapons['weaponDat']['sha256'] = weapon_data_sha
        weapon_path.write_text(json.dumps(weapons, indent=2)+'\n')
        sha = map_effects.digest(weapon_path)
        campaign['weaponVisuals']['sha256'] = sha
        for row in campaign['originalResources']:
            if row['relative'] == 'metadata/weapon-visuals.json':row['sha256'] = sha
    previous = campaign.get('preservedAssetBookkeeping', {})
    campaign['preservedAssetBookkeeping'] = dict(sourceValidatedPedPins=sorted(set(refreshed+previous.get('sourceValidatedPedPins', []))),
        absentObsoleteWeaponEntries=sorted(obsolete | set(previous.get('absentObsoleteWeaponEntries', []))),
        explicitWeaponVisuals=sorted(set(visual_choices+previous.get('explicitWeaponVisuals', []))), sourceValidatedWeaponData=True,
        assetBytesChanged=False)
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
        breakable_effects={name:200 for name in damage['breakableBySourceModel']})
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
    reconcile_resource_records(output)
    return dict(stage=str(output), version=version, sharedAssets=preserved,
                physics=physics, effects=effects, damage=damage, gameExecuted=False)


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

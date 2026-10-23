"""Mount LCS map ObjectData rows using SA's native object consumers.

Doors use SA's native garage controller. Source CWeapon::BlowUpExplosiveThings
detonates IsExplosiveThingModel (barrel4/petrolpump/washgaspump) objects by
model; SA expresses explosive objects through its ObjectData explosion
selector. SA detonates on its damage threshold; the source one-hit trigger is
not reproduced.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('door_map', ROOT/'tools/prepare-modloader-map.py')
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

# ModelIndices.h IsExplosiveThingModel.
EXPLOSIVE_THING_MODEL_NAMES = ('barrel4', 'petrolpump', 'washgaspump')

# The LCS and SA CollisionDamageEffect enums are not interchangeable. These
# are the LCS values observed in its ObjectData table and their SA equivalents
# where the source effect has a general native behavior. Same-name native SA
# rows may provide a more precise nonzero SA effect at translation time.
OBJECT_DAMAGE_EFFECTS = {
    0: 0,       # none
    1: 200,     # changed-model state has no imported SA damaged atomic; remove on damage
    3: 200,     # traffic light: use SA's native damage/removal behavior
    50: 20,     # specialized LCS smash effects -> SA generic complete smash
    60: 20,
    70: 20,
    80: 20,
    100: 200,   # LCS black-bag break -> SA generic damaged-piece removal
}


def read_object_data(path, minimum_fields):
    """Read ObjectData rows up to the first '*' using the engine's token shape."""
    rows = {}
    for number, raw in enumerate(Path(path).read_text(encoding='utf-8-sig').splitlines(), 1):
        raw = raw.split(';', 1)[0].strip()
        if not raw:
            continue
        if raw.startswith('*'):
            break
        if raw.startswith('#'):
            continue
        fields = tuple(re.split(r'[\s,]+', raw))
        if len(fields) < minimum_fields:
            raise ValueError(f'Incomplete ObjectData row at {path}:{number}')
        rows[fields[0].lower()] = fields
    return rows


def _integer_field(fields, index, label):
    try:
        return int(fields[index], 10)
    except (IndexError, TypeError, ValueError) as exc:
        raise ValueError(f'Invalid {label} in ObjectData row') from exc


def translate_object_data(source_fields, runtime_name, native_fields=None, explosive=False):
    """Translate one 11-token LCS row into an SA ObjectData row.

    `native_fields`, when supplied, is a tokenized same-name SA row. The return
    value is a token tuple: name, seven shared physics fields, translated
    damage effect, LCS/SA-compatible special response, camera flag, and SA's
    explosion/FX suffix.
    """
    if len(source_fields) < 11:
        raise ValueError('Incomplete LCS ObjectData row')
    if not runtime_name or any(character.isspace() or character == ',' for character in runtime_name):
        raise ValueError('Invalid runtime ObjectData model name')
    try:
        for value in source_fields[1:8]:
            float(value)
    except ValueError as exc:
        raise ValueError('Invalid LCS ObjectData physics field') from exc

    lcs_effect = _integer_field(source_fields, 8, 'collision damage effect')
    if lcs_effect not in OBJECT_DAMAGE_EFFECTS:
        raise ValueError(f'Unmapped LCS collision damage effect: {lcs_effect}')
    damage_effect = OBJECT_DAMAGE_EFFECTS[lcs_effect]
    if lcs_effect and native_fields is not None:
        native_effect = _integer_field(native_fields, 8, 'native collision damage effect')
        if not 0 <= native_effect <= 255:
            raise ValueError(f'Invalid native collision damage effect: {native_effect}')
        if native_effect == 1:
            # SA effect 1 also selects a damaged atomic. The map converter
            # deliberately keeps only the LCS near atomic, so this would
            # otherwise set a render state with no damaged geometry.
            native_effect = 200
        if native_effect:
            damage_effect = native_effect

    special_response = _integer_field(source_fields, 9, 'special collision response')
    if special_response not in (0, 1, 2, 3, 4):
        raise ValueError(f'Unmapped LCS special collision response: {special_response}')
    camera_avoid = _integer_field(source_fields, 10, 'camera avoidance')
    if camera_avoid not in (0, 1):
        raise ValueError(f'Invalid LCS camera avoidance flag: {camera_avoid}')

    if explosive:
        suffix = ('1', '2', '0.0', '0.0', '0.0', 'explosion_medium')
    else:
        suffix = ('0', '0', '0.0', '0.0', '0.0', 'none')
    return (runtime_name, *source_fields[1:8], str(damage_effect),
            str(special_response), str(camera_avoid), *suffix)


def legacy_inputs(package, original):
    definitions = {}
    for raw in (original/'DATA/OBJECT.DAT').read_text().splitlines():
        raw = raw.split(';')[0].strip()
        if not raw or raw.startswith(('#', '*')):
            continue
        fields = re.split(r'[\s,]+', raw)
        if fields[0].lower() in maps.PHYSICAL_DOOR_MODEL_NAMES or fields[0].lower() in EXPLOSIVE_THING_MODEL_NAMES:
            if len(fields) < 11:
                raise ValueError('Incomplete original door/explosive ObjectData: '+fields[0])
            definitions[fields[0].lower()] = fields[1:11]
    bindings = {}
    explosives = {}
    for raw in (package/'data/lcs/model-bindings.tsv').read_text().splitlines():
        source, native, name, alias = raw.split('\t')
        if name.lower() in EXPLOSIVE_THING_MODEL_NAMES and name.lower() in definitions:
            explosives[alias] = name.lower()
        # Legacy names remain in IsModelIndexADoor although their ObjectData
        # row is commented out or absent (e.g. impex_door). They are not a
        # source dynamic door and must keep their original static placement.
        if name.lower() in maps.PHYSICAL_DOOR_MODEL_NAMES and name.lower() in definitions:
            bindings[alias] = (source, native, name, alias)
    placements = []
    for path in sorted((package/'data/lcs').glob('world[012].ipl')):
        section = ''; index = 0
        for raw in path.read_text().splitlines():
            raw = raw.strip()
            if not raw or raw.startswith('#'):
                continue
            if ',' not in raw:
                section = raw.lower(); continue
            if section != 'inst':
                continue
            fields = [v.strip() for v in raw.split(',')]
            if fields[1] in bindings:
                item = bindings[fields[1]]
                if fields[0] != item[1]:
                    raise ValueError('Door IPL identity mismatch')
                placements.append('\t'.join((*item, path.name, str(index), *fields[3:6], 'object')))
            index += 1
    used = {line.split('\t')[3] for line in placements}
    if not used:
        raise ValueError('No original garage door placements')
    rows = ['; LCS garage doors and explosive things; merged by Modloader std.data with native OBJECT.DAT']
    for alias in sorted(used):
        item = bindings[alias]
        if item[2].lower() not in definitions:
            raise ValueError('Placed original garage ObjectData missing: '+item[2])
        # The ten original physical fields have the same SA meaning. SA adds
        # explosion/FX selectors and optional FX parameters; none are authored here.
        rows.append(', '.join((alias, *definitions[item[2].lower()], '0', '0', '0.0', '0.0', '0.0', 'none')))
    for alias in sorted(explosives):
        # Native explosion selector + destroyed FX of SA's own explosive barrels.
        rows.append(', '.join((alias, *definitions[explosives[alias]], '1', '2', '0.0', '0.0', '0.0', 'explosion_medium')))
    rows.append('*')
    return bindings, used, '\n'.join(placements)+'\n', '\n'.join(rows)+'\n'


def inputs(package, original):
    """Retain the pre-v2 input contract for older package audits."""
    return legacy_inputs(package, original)


def resolve_native_object_data(package, sa_objectdata=None):
    if sa_objectdata is False:
        return None
    if sa_objectdata is not None:
        path = Path(sa_objectdata)
        if not path.is_file():
            raise ValueError('Native SA ObjectData file is missing')
        return path
    preview = Path(package)/'metadata/map-preview.json'
    if preview.is_file():
        game_directory = json.loads(preview.read_text()).get('gameDirectory')
        if game_directory:
            path = Path(game_directory)/'data/object.dat'
            if path.is_file():
                return path
    return None


def object_data_inputs(package, original, sa_objectdata=None):
    """Build v2 ObjectData and the unchanged physical garage-door inventory."""
    source_rows = read_object_data(Path(original)/'DATA/OBJECT.DAT', 11)
    native_path = resolve_native_object_data(package, sa_objectdata)
    native_rows = read_object_data(native_path, 13) if native_path else {}

    all_bindings = {}
    for raw in (Path(package)/'data/lcs/model-bindings.tsv').read_text().splitlines():
        fields = raw.split('\t')
        if len(fields) != 4:
            raise ValueError('Invalid map model binding row')
        source, native, name, alias = fields
        key = name.lower()
        if key in all_bindings:
            raise ValueError('Duplicate source map model binding: '+name)
        all_bindings[key] = (source, native, name, alias)

    door_bindings = {
        item[3]: item for item in all_bindings.values()
        if item[2].lower() in maps.PHYSICAL_DOOR_MODEL_NAMES
        and item[2].lower() in source_rows
    }
    explosive_aliases = {
        item[3]: item[2].lower() for item in all_bindings.values()
        if item[2].lower() in EXPLOSIVE_THING_MODEL_NAMES
        and item[2].lower() in source_rows
    }
    placements = []
    for path in sorted((Path(package)/'data/lcs').glob('world[012].ipl')):
        section = ''; index = 0
        for raw in path.read_text().splitlines():
            raw = raw.strip()
            if not raw or raw.startswith('#'):
                continue
            if ',' not in raw:
                section = raw.lower(); continue
            if section != 'inst':
                continue
            fields = [value.strip() for value in raw.split(',')]
            if fields[1] in door_bindings:
                item = door_bindings[fields[1]]
                if fields[0] != item[1]:
                    raise ValueError('Door IPL identity mismatch')
                placements.append('\t'.join((*item, path.name, str(index), *fields[3:6], 'object')))
            index += 1
    used = {line.split('\t')[3] for line in placements}
    if not used:
        raise ValueError('No original garage door placements')

    output_rows = ['; LCS map ObjectData translated to SA fields; merged by Modloader std.data']
    object_aliases = []
    for item in sorted(all_bindings.values(), key=lambda row: row[3].lower()):
        source_name, alias = item[2].lower(), item[3]
        source_fields = source_rows.get(source_name)
        if source_fields is None:
            continue
        native_fields = native_rows.get(source_name) or native_rows.get(alias.lower())
        translated = translate_object_data(
            source_fields, alias, native_fields=native_fields,
            explosive=alias in explosive_aliases)
        output_rows.append(', '.join(translated))
        object_aliases.append(alias)
    output_rows.append('*')
    if len({alias.lower() for alias in object_aliases}) != len(object_aliases):
        raise ValueError('Duplicate runtime map ObjectData alias')
    if not set(used).issubset(object_aliases):
        raise ValueError('Placed original garage ObjectData missing')
    return (door_bindings, used, '\n'.join(placements)+'\n',
            '\n'.join(output_rows)+'\n', object_aliases, native_path)

def replace(path, data):
    # Package assets can be hardlinked to the approved rollback.
    temp = path.with_name(path.name+'.door-update')
    temp.write_bytes(data); temp.replace(path)

def mount(package, original, sa_objectdata=None):
    package = Path(package); original = Path(original)
    bindings, used, placements, objects, object_aliases, native_path = object_data_inputs(
        package, original, sa_objectdata)
    path = package/'data/lcs/world.ide'
    rows = []; changed = []
    section = ''
    for raw in path.read_text().splitlines():
        if raw in ('objs','tobj','txdp','end'):
            section = '' if raw == 'end' else raw
        elif section == 'objs':
            fields = [v.strip() for v in raw.split(',')]
            if fields[1] in used:
                fields[4] = str(int(fields[4], 0) | 0x800)
                raw = ', '.join(fields); changed.append(fields[1])
        rows.append(raw)
    if set(changed) != used or len(changed) != len(used):
        raise ValueError('Door IDE inventory mismatch')
    replace(path, ('\n'.join(rows)+'\n').encode('ascii'))
    replace(package/'data/object.dat', objects.encode('ascii'))
    replace(package/'data/lcs/door-bindings.tsv', placements.encode('ascii'))
    report = dict(formatVersion=2, source=str(original.resolve()),
                  sourceObjectDataSha256=digest(original/'DATA/OBJECT.DAT'),
                  nativeObjectDataPath=str(native_path.resolve()) if native_path else None,
                  nativeObjectDataSha256=digest(native_path) if native_path else None,
                  models=len(used), placements=len(placements.splitlines()),
                  aliases=sorted(used), objectDataModels=len(object_aliases),
                  objectDataAliases=sorted(object_aliases), gameExecuted=False,
                  files=[dict(relative=name, sha256=digest(package/name)) for name in
                         ('data/lcs/world.ide','data/object.dat','data/lcs/door-bindings.tsv')])
    relative = 'metadata/garage-doors.json'
    replace(package/relative, (json.dumps(report, indent=2)+'\n').encode())
    campaign_path = package/'metadata/campaign.json'
    campaign = json.loads(campaign_path.read_text())
    campaign['garageDoors'] = dict(manifest=relative, sha256=digest(package/relative))
    for row in campaign['preservedApprovedResources']:
        if row['relative'] == 'data/lcs/world.ide':
            row['sha256'] = digest(path)
    replace(campaign_path, json.dumps(campaign, indent=2).encode())
    return report

def audit(package):
    package = Path(package)
    campaign = json.loads((package/'metadata/campaign.json').read_text())
    pin = campaign['garageDoors']; manifest = package/pin['manifest']
    if digest(manifest) != pin['sha256']:
        raise ValueError('Garage door manifest changed')
    report = json.loads(manifest.read_text()); original = Path(report['source'])
    if digest(original/'DATA/OBJECT.DAT') != report['sourceObjectDataSha256']:
        raise ValueError('Original garage ObjectData changed')
    version = report.get('formatVersion', 1)
    if version == 1:
        bindings, used, placements, objects = legacy_inputs(package, original)
        object_aliases = None
    elif version == 2:
        native_path = report.get('nativeObjectDataPath')
        if native_path:
            native_path = Path(native_path)
            if digest(native_path) != report.get('nativeObjectDataSha256'):
                raise ValueError('Native SA ObjectData changed')
        else:
            native_path = False
        bindings, used, placements, objects, object_aliases, _ = object_data_inputs(
            package, original, native_path)
        if report.get('objectDataModels') != len(object_aliases) or \
                report.get('objectDataAliases') != sorted(object_aliases):
            raise ValueError('Map ObjectData inventory changed')
    else:
        raise ValueError('Unsupported garage/ObjectData manifest format')
    if (package/'data/object.dat').read_text() != objects or (package/'data/lcs/door-bindings.tsv').read_text() != placements:
        raise ValueError('Map ObjectData or garage identities differ from original inputs')
    if report['models'] != len(used) or report['placements'] != len(placements.splitlines()) or report['aliases'] != sorted(used):
        raise ValueError('Garage inventory changed')
    for row in report['files']:
        if digest(package/row['relative']) != row['sha256']:
            raise ValueError('Garage package input changed: '+row['relative'])
    marked = {fields[1] for section, fields in maps.definitions(package/'data/lcs/world.ide')
              if section == 'objs' and fields[1] in used and int(fields[4], 0)&0x800}
    if marked != used:
        raise ValueError('Native garage door model flag missing')
    result = dict(valid=True, models=len(used), placements=len(placements.splitlines()), gameExecuted=False)
    if object_aliases is not None:
        result['objectDataModels'] = len(object_aliases)
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--original', type=Path, default=ROOT/'out/lcs-converted')
    parser.add_argument('--sa-objectdata', type=Path)
    parser.add_argument('--audit', action='store_true')
    args = parser.parse_args()
    print(json.dumps(audit(args.package) if args.audit else mount(
        args.package, args.original, args.sa_objectdata), indent=2))

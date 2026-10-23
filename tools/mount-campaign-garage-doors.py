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
from collections.abc import Mapping

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('door_map', ROOT/'tools/prepare-modloader-map.py')
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

# ModelIndices.h IsExplosiveThingModel.
EXPLOSIVE_THING_MODEL_NAMES = ('barrel4', 'petrolpump', 'washgaspump')

# The LCS and SA CollisionDamageEffect enums are not interchangeable. Effects
# 1 and 3 in LCS request a damaged model; SA uses effect 1 for its damaged
# atomic path. They are only enabled when the converter proves an `_dam`
# atomic exists. Effects 50/60/70/80/100 request complete object-break
# behavior in LCS. SA effects 20/200/202 have different consumers, and the
# geometry plugin alone does not prove that a runtime fragment consumer is
# present. Keep the native collision physics unless the caller supplies both
# generated breakable geometry and an explicitly proven SA effect.
OBJECT_DAMAGE_EFFECTS = {
    0: 0,       # none
    1: 0,       # conditional: SA damaged atomic (effect 1) only when available
    3: 0,       # conditional: damage/toggle traffic light via SA damaged atomic
    50: 0,       # conditional: requires a runtime fragment consumer
    60: 0,
    70: 0,
    80: 0,
    100: 0,
}

LEGACY_V2_OBJECT_DAMAGE_EFFECTS = {
    0: 0, 1: 200, 3: 200, 50: 20, 60: 20, 70: 20, 80: 20, 100: 200,
}

LEGACY_V3_OBJECT_DAMAGE_EFFECTS = {
    0: 0, 1: 0, 3: 0, 50: 20, 60: 20, 70: 20, 80: 20, 100: 200,
}
LCS_BREAKABLE_EFFECTS = (50, 60, 70, 80, 100)
SA_FRAGMENT_EFFECTS = (200, 202)


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


def _translate_object_data(source_fields, runtime_name, native_fields=None,
                           explosive=False, damaged_atomic_available=False,
                           breakable_effect=None, legacy_version=0):
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
    legacy_v2 = legacy_version == 2
    legacy_v3 = legacy_version == 3
    if legacy_version not in (0, 2, 3):
        raise ValueError(f'Unsupported legacy ObjectData translation: {legacy_version}')
    if breakable_effect is not None:
        breakable_effect = int(breakable_effect)
        if lcs_effect not in LCS_BREAKABLE_EFFECTS and lcs_effect != 3:
            raise ValueError('SA fragment effect supplied for non-breakable LCS effect')
        if breakable_effect not in SA_FRAGMENT_EFFECTS:
            raise ValueError(f'Unsupported SA fragment effect: {breakable_effect}')

    if legacy_v2:
        effect_map = LEGACY_V2_OBJECT_DAMAGE_EFFECTS
    elif legacy_v3:
        effect_map = LEGACY_V3_OBJECT_DAMAGE_EFFECTS
    else:
        effect_map = OBJECT_DAMAGE_EFFECTS
    damage_effect = effect_map[lcs_effect]
    if legacy_v3 and lcs_effect in (1, 3):
        damage_effect = 1 if damaged_atomic_available else 0
    elif not (legacy_v2 or legacy_v3) and lcs_effect in (1, 3):
        damage_effect = 1 if damaged_atomic_available else 0
        if lcs_effect == 3 and not damaged_atomic_available and breakable_effect is not None:
            # LCS smashes a traffic light; without a damaged atomic the SA
            # breakable fragments are the nearest generated equivalent.
            damage_effect = breakable_effect
    elif not (legacy_v2 or legacy_v3) and lcs_effect in LCS_BREAKABLE_EFFECTS:
        damage_effect = breakable_effect if breakable_effect is not None else 0

    if (legacy_v2 or legacy_v3) and lcs_effect and native_fields is not None and \
            (legacy_v2 or lcs_effect not in (1, 3)):
        native_effect = _integer_field(native_fields, 8, 'native collision damage effect')
        if not 0 <= native_effect <= 255:
            raise ValueError(f'Invalid native collision damage effect: {native_effect}')
        if legacy_v2 and native_effect == 1:
            # SA effect 1 also selects a damaged atomic. The map converter
            # deliberately keeps only the LCS near atomic, so this would
            # otherwise set a render state with no damaged geometry.
            native_effect = 200
        elif legacy_v3 and native_effect == 1 and not damaged_atomic_available:
            # SA effect 1 activates the damaged-atomic path. Never set it
            # based only on a same-name ObjectData row.
            native_effect = 0
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
    elif not (legacy_v2 or legacy_v3) and source_fields[0].lower() == 'fire_hydrant':
        # SA FX_TYPE 1 plays the named FxSystem when the object is hit.
        suffix = ('0', '1', '0.0', '0.0', '0.0', 'water_hydrant')
    else:
        suffix = ('0', '0', '0.0', '0.0', '0.0', 'none')
    return (runtime_name, *source_fields[1:8], str(damage_effect),
            str(special_response), str(camera_avoid), *suffix)


def translate_object_data(source_fields, runtime_name, native_fields=None,
                          explosive=False, damaged_atomic_available=False,
                          breakable_effect=None):
    """Translate one LCS row, opting into SA damage effect 1 only with `_dam` geometry.

    `damaged_atomic_available` must be backed by the map converter's verified
    source-model-to-damaged-atomic relation. LCS effects 1 and 3 then become SA
    effect 1; without that geometry they become effect 0 while the source
    physical fields, including uproot threshold, remain in force. Complete
    LCS break effects stay at SA effect 0 unless `breakable_effect` is 200 or
    202 and the caller has separately proven both generated breakable geometry
    and an SA runtime consumer for that model. A same-name SA row or generated
    plugin by itself is insufficient.
    """
    return _translate_object_data(
        source_fields, runtime_name, native_fields, explosive,
        damaged_atomic_available, breakable_effect, legacy_version=0)


def _translate_object_data_v2(source_fields, runtime_name, native_fields=None,
                              explosive=False):
    """Reproduce the pre-damaged-geometry v2 output for old manifest audits."""
    return _translate_object_data(
        source_fields, runtime_name, native_fields, explosive,
        damaged_atomic_available=False, legacy_version=2)


def _translate_object_data_v3(source_fields, runtime_name, native_fields=None,
                              explosive=False, damaged_atomic_available=False):
    """Reproduce the pre-breakable-contract v3 output for old manifest audits."""
    return _translate_object_data(
        source_fields, runtime_name, native_fields, explosive,
        damaged_atomic_available, legacy_version=3)


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


def object_data_inputs(package, original, sa_objectdata=None,
                       damaged_atomic_names=None, breakable_names=None,
                       breakable_effects=None, legacy_v2=False,
                       legacy_v3=False):
    """Build translated ObjectData and its proven damage-geometry inventories.

    `breakable_names` inventories generated fragment geometry. It does not
    enable a SA break effect. `breakable_effects` is an explicit per-source-
    model map to SA effect 200 or 202, and is accepted only for a source model
    with generated geometry. The caller must supply that map only after
    proving an installed runtime consumer for the selected SA effect.
    """
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

    # The converter inventory is expressed in source model names. Accepting a
    # runtime alias as well makes the boundary convenient for callers that have
    # already joined the map binding table; persisted names are canonicalized
    # source names so audits can reproduce the exact translation.
    if isinstance(damaged_atomic_names, (str, bytes)):
        raise ValueError('Damaged-atomic names must be an iterable of model names')
    supplied_damage_names = {
        str(name).lower() for name in (damaged_atomic_names or ())
    }
    supported_source_names = set()
    unsupported_source_names = set()
    for item in all_bindings.values():
        source_name, alias = item[2].lower(), item[3].lower()
        source_fields = source_rows.get(source_name)
        if source_fields is None or _integer_field(source_fields, 8, 'collision damage effect') not in (1, 3):
            continue
        if source_name in supplied_damage_names or alias in supplied_damage_names:
            supported_source_names.add(source_name)
        else:
            unsupported_source_names.add(source_name)

    # Geometry/plugin availability is deliberately separate from selection of
    # SA effects 200/202: those effects hide the original model and only make
    # sense when a runtime consumer actually emits the fragments.
    if isinstance(breakable_names, (str, bytes)):
        raise ValueError('Breakable names must be an iterable of model names')
    supplied_breakable_names = {
        str(name).lower() for name in (breakable_names or ())
    }
    breakable_candidates = {}
    source_or_alias_to_source = {}
    for item in all_bindings.values():
        source_name, alias = item[2].lower(), item[3].lower()
        source_or_alias_to_source[source_name] = source_name
        source_or_alias_to_source[alias] = source_name
        source_fields = source_rows.get(source_name)
        if source_fields is None:
            continue
        source_effect = _integer_field(source_fields, 8, 'collision damage effect')
        if source_effect in LCS_BREAKABLE_EFFECTS or source_effect == 3:
            breakable_candidates[source_name] = alias
    supported_breakable_source_names = {
        name for name, alias in breakable_candidates.items()
        if name in supplied_breakable_names or alias.lower() in supplied_breakable_names
    }
    unsupported_breakable_source_names = (
        set(breakable_candidates) - supported_breakable_source_names)

    if breakable_effects is None:
        breakable_effects = {}
    if not isinstance(breakable_effects, Mapping):
        raise ValueError('Breakable effects must map source model names to SA effects')
    normalized_breakable_effects = {}
    for raw_name, raw_effect in breakable_effects.items():
        key = str(raw_name).lower()
        source_name = source_or_alias_to_source.get(key)
        if source_name is None or source_name not in breakable_candidates:
            raise ValueError('Breakable effect references a model without LCS break behavior: '+str(raw_name))
        if source_name not in supported_breakable_source_names:
            raise ValueError('Breakable effect references a model without generated breakable geometry: '+str(raw_name))
        try:
            effect = int(raw_effect)
        except (TypeError, ValueError) as exc:
            raise ValueError('Invalid SA breakable effect for '+str(raw_name)) from exc
        if effect not in SA_FRAGMENT_EFFECTS:
            raise ValueError('SA breakable effects must be 200 or 202')
        if source_name in normalized_breakable_effects:
            raise ValueError('Duplicate breakable effect source model: '+source_name)
        normalized_breakable_effects[source_name] = effect

    if legacy_v2 and legacy_v3:
        raise ValueError('Only one historical ObjectData format can be audited at a time')
    legacy_mode = legacy_v2 or legacy_v3
    if legacy_mode and (breakable_names or breakable_effects):
        raise ValueError('Historical ObjectData audit cannot accept breakable metadata')

    output_rows = ['; LCS map ObjectData translated to SA fields; merged by Modloader std.data']
    object_aliases = []
    damage_required_aliases = []
    breakable_required_aliases = []
    for item in sorted(all_bindings.values(), key=lambda row: row[3].lower()):
        source_name, alias = item[2].lower(), item[3]
        source_fields = source_rows.get(source_name)
        if source_fields is None:
            continue
        native_fields = native_rows.get(source_name) or native_rows.get(alias.lower())
        damage_atomic_available = source_name in supported_source_names
        if legacy_v2:
            translator = _translate_object_data_v2
        elif legacy_v3:
            translator = _translate_object_data_v3
        else:
            translator = translate_object_data
        if legacy_v2:
            translated = translator(
                source_fields, alias, native_fields=native_fields,
                explosive=alias in explosive_aliases)
        elif legacy_v3:
            translated = translator(
                source_fields, alias, native_fields=native_fields,
                explosive=alias in explosive_aliases,
                damaged_atomic_available=damage_atomic_available)
        else:
            translated = translator(
                source_fields, alias, native_fields=native_fields,
                explosive=alias in explosive_aliases,
                damaged_atomic_available=damage_atomic_available,
                breakable_effect=normalized_breakable_effects.get(source_name))
        output_rows.append(', '.join(translated))
        object_aliases.append(alias)
        if translated[8] == '1':
            damage_required_aliases.append(alias)
        if translated[8] in ('200', '202') and source_name in breakable_candidates:
            breakable_required_aliases.append(alias)
    output_rows.append('*')
    if len({alias.lower() for alias in object_aliases}) != len(object_aliases):
        raise ValueError('Duplicate runtime map ObjectData alias')
    if not set(used).issubset(object_aliases):
        raise ValueError('Placed original garage ObjectData missing')
    return (door_bindings, used, '\n'.join(placements)+'\n',
            '\n'.join(output_rows)+'\n', object_aliases, native_path,
            damage_required_aliases, sorted(supported_source_names),
            sorted(unsupported_source_names), sorted(supported_breakable_source_names),
            sorted(unsupported_breakable_source_names), breakable_required_aliases,
            dict(sorted(normalized_breakable_effects.items())))

def replace(path, data):
    # Package assets can be hardlinked to the approved rollback.
    temp = path.with_name(path.name+'.door-update')
    temp.write_bytes(data); temp.replace(path)

def mount(package, original, sa_objectdata=None, damaged_atomic_names=None,
          breakable_names=None, breakable_effects=None):
    package = Path(package); original = Path(original)
    (bindings, used, placements, objects, object_aliases, native_path,
     damage_required_aliases, damage_source_names,
     damage_unavailable_source_names, breakable_source_names,
     breakable_unavailable_source_names, breakable_required_aliases,
     breakable_effect_map) = object_data_inputs(
        package, original, sa_objectdata, damaged_atomic_names,
        breakable_names, breakable_effects)
    source_aliases = {}
    for raw in (package/'data/lcs/model-bindings.tsv').read_text().splitlines():
        fields = raw.split('\t')
        if len(fields) == 4:
            source_aliases[fields[2].lower()] = fields[3]
    damage_unavailable_aliases = sorted(
        source_aliases[name] for name in damage_unavailable_source_names
        if name in source_aliases)
    breakable_unavailable_aliases = sorted(
        source_aliases[name] for name in breakable_unavailable_source_names
        if name in source_aliases)
    damage_required = set(damage_required_aliases)
    path = package/'data/lcs/world.ide'
    rows = []; door_changed = []; damage_changed = []; other_damage_defs = []
    section = ''
    for raw in path.read_text().splitlines():
        if raw in ('objs','tobj','txdp','end'):
            section = '' if raw == 'end' else raw
        elif section in ('objs', 'tobj') and ',' in raw:
            fields = [v.strip() for v in raw.split(',')]
            if len(fields) > 4 and fields[1] in damage_required and section != 'objs':
                other_damage_defs.append(fields[1])
            if len(fields) > 4 and (fields[1] in used or fields[1] in damage_required or
                                    fields[1] in damage_unavailable_aliases) and section in ('objs', 'tobj'):
                flags = int(fields[4], 0)
                if fields[1] in used:
                    if section != 'objs':
                        raise ValueError('Garage door model cannot use an SA time-object definition: '+fields[1])
                    flags |= 0x800
                    door_changed.append(fields[1])
                if fields[1] in damage_required:
                    if section != 'objs':
                        continue
                    # SA LoadObject routes this IDE bit to DamageAtomicModelInfo;
                    # the converter must provide a related `{model}_dam` atomic.
                    flags |= 0x1000
                    damage_changed.append(fields[1])
                else:
                    # Do not inherit a source-side bit with SA's damaged-atomic
                    # meaning when the converter did not prove `{model}_dam`.
                    flags &= ~0x1000
                fields[4] = str(flags)
                raw = ', '.join(fields)
        rows.append(raw)
    if other_damage_defs:
        raise ValueError('Damaged map models cannot use SA time-object definitions: '+', '.join(sorted(other_damage_defs)))
    if set(door_changed) != used or len(set(door_changed)) != len(used):
        raise ValueError('Door IDE inventory mismatch')
    if set(damage_changed) != damage_required or len(damage_changed) != len(damage_required):
        raise ValueError('Damaged-atomic IDE inventory mismatch')
    replace(path, ('\n'.join(rows)+'\n').encode('ascii'))
    replace(package/'data/object.dat', objects.encode('ascii'))
    replace(package/'data/lcs/door-bindings.tsv', placements.encode('ascii'))
    report = dict(formatVersion=4, source=str(original.resolve()),
                  sourceObjectDataSha256=digest(original/'DATA/OBJECT.DAT'),
                  nativeObjectDataPath=str(native_path.resolve()) if native_path else None,
                  nativeObjectDataSha256=digest(native_path) if native_path else None,
                  models=len(used), placements=len(placements.splitlines()),
                  aliases=sorted(used), objectDataModels=len(object_aliases),
                  objectDataAliases=sorted(object_aliases),
                  damagedAtomicConvention='IDE 0x1000; base frame {model}; damaged atomic {model}_dam',
                  damageEffect3Adaptation='SA effect 1 damaged atomic; LCS metal debris and collision sound are not reproduced',
                  damagedAtomicSourceModels=damage_source_names,
                  damageAtomicRequiredAliases=sorted(damage_required),
                  damageAtomicUnavailableAliases=damage_unavailable_aliases,
                  breakableGeometrySourceModels=breakable_source_names,
                  breakableGeometryUnavailableAliases=breakable_unavailable_aliases,
                  breakableRequiredAliases=sorted(breakable_required_aliases),
                  breakableEffectBySourceModel=breakable_effect_map,
                  breakableEffectContract='Effects 200/202 require generated geometry and an explicitly proven runtime consumer; geometry plugin alone never enables the effect',
                  gameExecuted=False,
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
        (bindings, used, placements, objects, object_aliases, *_) = object_data_inputs(
            package, original, native_path, legacy_v2=True)
        if report.get('objectDataModels') != len(object_aliases) or \
                report.get('objectDataAliases') != sorted(object_aliases):
            raise ValueError('Map ObjectData inventory changed')
        damage_required_aliases = None
        damage_unavailable_aliases = None
    elif version in (3, 4):
        native_path = report.get('nativeObjectDataPath')
        if native_path:
            native_path = Path(native_path)
            if digest(native_path) != report.get('nativeObjectDataSha256'):
                raise ValueError('Native SA ObjectData changed')
        else:
            native_path = False
        damage_source_names = report.get('damagedAtomicSourceModels', [])
        if version == 3:
            (bindings, used, placements, objects, object_aliases, _,
             damage_required_aliases, actual_damage_source_names,
             damage_unavailable_source_names, *_) = object_data_inputs(
                package, original, native_path, damage_source_names,
                legacy_v3=True)
        else:
            breakable_geometry_names = report.get('breakableGeometrySourceModels', [])
            breakable_effect_map = report.get('breakableEffectBySourceModel', {})
            (bindings, used, placements, objects, object_aliases, _,
             damage_required_aliases, actual_damage_source_names,
             damage_unavailable_source_names, actual_breakable_geometry_names,
             breakable_unavailable_source_names, breakable_required_aliases,
             actual_breakable_effect_map) = object_data_inputs(
                package, original, native_path, damage_source_names,
                breakable_geometry_names, breakable_effect_map)
        if report.get('objectDataModels') != len(object_aliases) or \
                report.get('objectDataAliases') != sorted(object_aliases):
            raise ValueError('Map ObjectData inventory changed')
        if actual_damage_source_names != sorted(set(damage_source_names)):
            raise ValueError('Damaged-atomic source inventory changed')
        source_aliases = {}
        for raw in (package/'data/lcs/model-bindings.tsv').read_text().splitlines():
            fields = raw.split('\t')
            if len(fields) == 4:
                source_aliases[fields[2].lower()] = fields[3]
        damage_unavailable_aliases = sorted(
            source_aliases[name] for name in damage_unavailable_source_names
            if name in source_aliases)
        if report.get('damagedAtomicConvention') != \
                'IDE 0x1000; base frame {model}; damaged atomic {model}_dam' or \
                report.get('damageEffect3Adaptation') != \
                'SA effect 1 damaged atomic; LCS metal debris and collision sound are not reproduced' or \
                report.get('damageAtomicRequiredAliases') != sorted(damage_required_aliases) or \
                report.get('damageAtomicUnavailableAliases') != damage_unavailable_aliases:
            raise ValueError('Damaged-atomic inventory or naming contract changed')
        if version == 4 and (
                actual_breakable_geometry_names != sorted(set(breakable_geometry_names)) or
                actual_breakable_effect_map != dict(sorted(breakable_effect_map.items())) or
                report.get('breakableGeometryUnavailableAliases') !=
                    sorted(source_aliases[name] for name in breakable_unavailable_source_names
                           if name in source_aliases) or
                report.get('breakableRequiredAliases') != sorted(breakable_required_aliases) or
                report.get('breakableEffectContract') !=
                    'Effects 200/202 require generated geometry and an explicitly proven runtime consumer; geometry plugin alone never enables the effect'):
            raise ValueError('Breakable geometry/effect inventory or runtime contract changed')
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
    if version in (3, 4):
        flagged_damage_aliases = {
            fields[1] for section, fields in maps.definitions(package/'data/lcs/world.ide')
            if section == 'objs' and int(fields[4], 0)&0x1000
        }
        if not set(damage_required_aliases).issubset(flagged_damage_aliases) or \
                set(damage_unavailable_aliases).intersection(flagged_damage_aliases):
            raise ValueError('Native damaged-atomic IDE flag missing or unsupported')
    if version == 4:
        # The breakable plugin has no IDE bit in the retail loader path. Only
        # ObjectData effects explicitly supplied with a proven runtime consumer
        # can request SA fragment handling.
        breakable_geometry_aliases = {
                source_aliases[name] for name in report.get('breakableGeometrySourceModels', [])
                if name in source_aliases}
        if not set(breakable_required_aliases).issubset(breakable_geometry_aliases):
            raise ValueError('Breakable effect lacks generated geometry')
    result = dict(valid=True, models=len(used), placements=len(placements.splitlines()), gameExecuted=False)
    if object_aliases is not None:
        result['objectDataModels'] = len(object_aliases)
    if version in (3, 4):
        result['damageAtomicModels'] = len(damage_required_aliases)
        result['damageAtomicUnavailableModels'] = len(damage_unavailable_aliases)
    if version == 4:
        result['breakableGeometryModels'] = len(report.get('breakableGeometrySourceModels', []))
        result['breakableEffectModels'] = len(breakable_required_aliases)
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

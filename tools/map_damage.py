"""Export source-proven LCS damaged atomics and SA breakable geometry plugins.

The source model table chooses whether atomic index 1 is the damaged model.
This module additionally requires the streamed source DFF to contain exactly
the matching `_L0`, `_L1` atomics in that order. An `_L1` suffix alone is never
treated as damage geometry. Reports contain names and counts, not extracted
geometry, coordinates, or source effect tables.
"""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import hashlib

from ps2_effects import SA_2DFX_PLUGIN
from rwgeom import BREAKABLE_PLUGIN, copy_geometry_plugins, geometry_plugins, inject_breakable
from rwtree import parse as parse_rw


def _walk(nodes):
    for node in nodes:
        yield node
        yield from _walk(node.get('kids', ()))


def _atomic_names(converter, source_dff):
    completed = subprocess.run(
        [str(converter), str(source_dff), '--info'],
        capture_output=True, text=True, check=False)
    if completed.returncode:
        raise ValueError('source DFF inspection failed')
    names = re.findall(r'^atomic=([^\s]+)\s+vertices=', completed.stdout, re.MULTILINE)
    if not names:
        raise ValueError('source DFF has no named atomics')
    return names


def _source_damage(row):
    """Return a reason when source metadata does not prove the last atomic damaged.

    Two atomics: draw distances must not increase. Three atomics: the middle
    one must be a farther LOD (larger distance) and the last one nearer than
    the near atomic, which is how a damaged mesh is told apart from a LOD.
    """
    if row.get('type') not in (1, 3):
        return 'source row is not an object or time object'
    if row.get('flags', 0) & 0x10:
        return 'source model is marked as an LOD'
    count = row.get('atomics')
    if count not in (2, 3):
        return 'source model does not declare two or three atomics'
    distances = row.get('draw')
    if not isinstance(distances, (tuple, list)) or len(distances) < count:
        return 'source model lacks its draw distances'
    if any(not math.isfinite(float(value)) for value in distances[:count]):
        return 'source model has nonfinite draw distances'
    if count == 2:
        if float(distances[0]) < float(distances[1]):
            return 'source draw distances do not mark atomic 1 as damaged'
    elif not (float(distances[1]) > float(distances[0]) and float(distances[2]) <= float(distances[0])):
        return 'source draw distances do not mark atomic 2 as damaged beyond a far LOD'
    return None


def _source_name(row):
    name = row.get('name')
    if not isinstance(name, str) or not name:
        return None
    try:
        name.encode('ascii')
    except UnicodeEncodeError:
        return None
    return name


def _safe_asset_name(value):
    if not isinstance(value, str) or not value or not re.fullmatch(r'[A-Za-z0-9_]+', value):
        return False
    try:
        value.encode('ascii')
    except UnicodeEncodeError:
        return False
    return True


def _atomic_replace(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', suffix='.pending', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if path.exists():
            os.chmod(temporary, path.stat().st_mode & 0o777)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def _write_report(path, report):
    if path is None:
        return
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def _geometry_count(raw):
    roots = parse_rw(raw)
    return sum(node['id'] == 15 for node in _walk(roots))


def export_damage(game, models, assets, streamed_source, converter, lighting,
                  output_report=None, breakable_names=(), write_assets=True):
    """Patch current map DFFs with proven `_dam` atomics and breakable plugins.

    `models` is the principal model map keyed by source model ID; each record's
    `name` is the current package/runtime asset name, which may be an alias.
    The streamed DFF lookup always uses `game.models[id]['name']`. The existing
    target DFF's first-geometry SA 2DFX plugin is copied byte-for-byte to the
    converted near geometry before any replacement is written.

    `breakable_names` is an iterable of original source model names or integer
    model IDs supplied by the private ObjectData/effect mapper. Every existing
    geometry in each selected target DFF receives the registered Rockstar
    ID_BREAKABLE (0x253F2FD) plugin from its own RenderWare vertices, UVs,
    colors, triangles, materials and surface properties. No proxy geometry is
    created. The API writes replacements atomically by default and returns a
    source-name-keyed inventory for the IDE/mount audit.
    """
    asset_dir = Path(assets)
    streamed_dir = Path(streamed_source)
    converter = Path(converter)
    lighting = Path(lighting)
    replacements = {}
    result = dict(format=1, gameExecuted=False, damageAtomicBySourceModel={}, breakableBySourceModel={},
                  unsupported=[], convertedDamagePairs=0, breakableModels=0,
                  breakableGeometries=0, preserved2dfxPlugins=0,
                  pluginBreakableFD=0)
    result['inputs'] = dict(streamedSource=str(streamed_dir.resolve()), converter=str(converter.resolve()),
        lighting=str(lighting.resolve()), lightingSha256=hashlib.sha256(lighting.read_bytes()).hexdigest())

    selected_breakables = set()
    by_name = {}
    for model_id, source in game.models.items():
        name = _source_name(source)
        if name:
            by_name[name.casefold()] = model_id
    if isinstance(breakable_names, str):
        breakable_names = (breakable_names,)
    for item in breakable_names:
        if isinstance(item, int):
            selected_breakables.add(item)
        elif isinstance(item, str):
            model_id = by_name.get(item.casefold())
            if model_id is None:
                result['unsupported'].append(dict(sourceModel=item,
                    feature='breakable', reason='source model name is absent'))
            else:
                selected_breakables.add(model_id)
        else:
            result['unsupported'].append(dict(sourceModel=str(item),
                feature='breakable', reason='source model selector must be a name or ID'))

    damage_candidates = {}
    for model_id, source in sorted(game.models.items()):
        row = _source_damage(source)
        distances = source.get('draw', ())
        # Keep the audit scoped to non-LOD models whose source metadata shows
        # the descending-distance convention used by CFileLoader::LoadObject.
        if source.get('type') not in (1, 3) or source.get('flags', 0) & 0x10:
            continue
        atomic_count = source.get('atomics', 0)
        if atomic_count < 2 or atomic_count > 3 or not isinstance(distances, (tuple, list)) or len(distances) < atomic_count:
            continue
        if any(not math.isfinite(float(value)) for value in distances[:atomic_count]):
            continue
        if atomic_count == 2:
            source_marks_damage = float(distances[0]) >= float(distances[1])
        else:
            source_marks_damage = not (float(distances[0]) < float(distances[1]) and
                                       float(distances[1]) < float(distances[2]))
        if not source_marks_damage:
            continue
        source_name = _source_name(source)
        if not source_name:
            result['unsupported'].append(dict(modelId=model_id, feature='damageAtomic',
                reason='source model name is missing or non-ASCII'))
            continue
        if row:
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                feature='damageAtomic', reason=row))
            continue
        damage_candidates[model_id] = source_name

    for model_id, source_name in damage_candidates.items():
        record = models.get(model_id)
        if not isinstance(record, dict) or not record.get('name'):
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                feature='damageAtomic', reason='no principal converted model asset'))
            continue
        target_name = str(record['name'])
        if not _safe_asset_name(target_name):
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='damageAtomic', reason='target asset name is not path-safe ASCII'))
            continue
        target_path = asset_dir / (target_name + '.dff')
        source_dff = streamed_dir / (source_name + '.dff')
        if not source_dff.is_file():
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='damageAtomic', reason='streamed source DFF is absent'))
            continue
        if not target_path.is_file():
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='damageAtomic', reason='current target DFF is absent'))
            continue
        if len(target_name.encode('ascii')) > 19:
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='damageAtomic',
                reason='target model name cannot fit SA 24-byte _dam atomic name'))
            continue
        try:
            atomic_names = _atomic_names(converter, source_dff)
            declared = int(game.models[model_id].get('atomics', 0))
            if len(atomic_names) != declared:
                raise ValueError('streamed source DFF atomic count differs from its model row')
            near_name = atomic_names[0].casefold()
            damaged_name = atomic_names[-1].casefold()
            if not near_name.endswith('_l0') or damaged_name != near_name[:-3] + '_l' + str(declared - 1) or                     (declared == 3 and atomic_names[1].casefold() != near_name[:-3] + '_l1'):
                raise ValueError('streamed source atomics do not prove the `_L0`..`_L' + str(declared - 1) + '` ordering')
            old_raw = target_path.read_bytes()
            if not _geometry_count(old_raw):
                raise ValueError('current target DFF has no geometry')
            with tempfile.TemporaryDirectory(prefix='.damage-convert-', dir=asset_dir) as temporary:
                converted_path = Path(temporary) / 'converted.dff'
                command = [str(converter), str(source_dff), str(converted_path),
                           '--atomic-damage', target_name, atomic_names[0],
                           atomic_names[-1], str(lighting)]
                completed = subprocess.run(command, capture_output=True, text=True, check=False)
                if completed.returncode:
                    raise ValueError('native converter rejected the source pair')
                converted = converted_path.read_bytes()
            # The effects exporter attaches SA 2DFX to geometry zero. Carry its
            # exact plugin chunk to the new near geometry without translating
            # or dropping its payload.
            old_plugins = geometry_plugins(old_raw, 0, {SA_2DFX_PLUGIN})
            if old_plugins:
                converted = copy_geometry_plugins(old_raw, converted, {SA_2DFX_PLUGIN}, 0, 0)
                result['preserved2dfxPlugins'] += 1
            previous = replacements.get(target_name)
            if previous is not None and previous != converted:
                raise ValueError('multiple source models resolve to the same target asset')
            damage_entry = dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, nearSourceAtomic=atomic_names[0],
                damagedSourceAtomic=atomic_names[-1], damageAtomicName=target_name + '_dam',
                sourceAtomicCount=declared, damageIndex=declared - 1, sourceSha256=hashlib.sha256(source_dff.read_bytes()).hexdigest(),
                evidence=('LCS distances are nonincreasing; source DFF order is _L0,_L1' if declared == 2 else
                          'LCS distances: _L1 is a farther LOD, _L2 is nearer than _L0; source DFF order is _L0,_L1,_L2'))
            replacements[target_name] = converted
            result['damageAtomicBySourceModel'][source_name.casefold()] = damage_entry
            result['convertedDamagePairs'] += 1
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='damageAtomic', reason=str(error)))

    for model_id in sorted(selected_breakables):
        source = game.models.get(model_id)
        source_name = _source_name(source or {})
        record = models.get(model_id)
        if not source_name:
            result['unsupported'].append(dict(modelId=model_id, feature='breakable',
                reason='source model row is absent or unnamed'))
            continue
        if not isinstance(record, dict) or not record.get('name'):
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                feature='breakable', reason='no principal converted model asset'))
            continue
        target_name = str(record['name'])
        if not _safe_asset_name(target_name):
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='breakable', reason='target asset name is not path-safe ASCII'))
            continue
        target_path = asset_dir / (target_name + '.dff')
        if target_name in replacements:
            raw = replacements[target_name]
        elif target_path.is_file():
            raw = target_path.read_bytes()
        else:
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='breakable', reason='current target DFF is absent'))
            continue
        try:
            before = geometry_plugins(raw, 0, {BREAKABLE_PLUGIN}) if _geometry_count(raw) else {}
            patched = inject_breakable(raw)
            count = _geometry_count(patched)
            plugin_count = 0
            for geometry_index in range(count):
                plugin_count += bool(geometry_plugins(patched, geometry_index, {BREAKABLE_PLUGIN}))
            if plugin_count != count:
                raise ValueError('Breakable plugin did not attach to every geometry')
            replacements[target_name] = patched
            entry = dict(modelId=model_id, sourceModel=source_name, targetAsset=target_name,
                pluginId=f'0x{BREAKABLE_PLUGIN:X}', geometries=count, generatedFrom='target DFF source geometry',
                alreadyPresent=bool(before))
            result['breakableBySourceModel'][source_name.casefold()] = entry
            result['breakableModels'] += 1
            result['breakableGeometries'] += count
            result['pluginBreakableFD'] += 1
        except (OSError, ValueError) as error:
            result['unsupported'].append(dict(modelId=model_id, sourceModel=source_name,
                targetAsset=target_name, feature='breakable', reason=str(error)))

    if write_assets:
        for target_name, raw in replacements.items():
            _atomic_replace(asset_dir / (target_name + '.dff'), raw)
    result['replacementAssets'] = sorted(replacements)
    _write_report(output_report, result)
    return result

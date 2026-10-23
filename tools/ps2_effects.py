"""Read LCS PS2 2DFX lights and attach SA Geometry effect plugins.

Source effect locations are offsets inside the bounded GAME.DTZ Chunk.  The
SA target uses the RenderWare Geometry plugin 0x253F2F8. Day/night flags and
temporal modes select SA's native light consumer. Native flicker rhythms are
an explicit adaptation rather than an exact recreation of Leeds' random seed.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import struct

from rwtree import parse as parse_rw


SA_2DFX_PLUGIN = 0x253F2F8
_EFFECT_LIGHT = 0
_EFFECT_RECORD_SIZE = 0x40
_LIGHT_MODES = (
    'on', 'on_night', 'flicker', 'flicker_night', 'flash1', 'flash1_night',
    'flash2', 'flash2_night', 'flash3', 'flash3_night',
    'random_flicker', 'random_flicker_night', 'special',
    'bridge_flash1', 'bridge_flash2',
)
_SA_LIGHT_ATTR = struct.Struct('<4B4f5B24s24s2B3b2x')
_EFFECT_HEADER = struct.Struct('<3fII')


def _span(chunk, offset, size, what):
    if offset < 0 or size < 0 or offset + size > len(chunk.data):
        raise ValueError(f'{what}: source field outside GAME.DTZ')
    return chunk.data[offset:offset + size]


def _source_texture(chunk, pointer, what):
    if not pointer:
        return ''
    # RslTexture: raster ptr, dictionary ptr, intrusive-list links, name[32].
    raw = _span(chunk, pointer, 48, what)
    name = raw[16:48].split(b'\0', 1)[0].decode('ascii')
    if len(name.encode('ascii')) > 23:
        raise ValueError(f'{what}: texture name does not fit SA 2DFX field')
    return name


def _read_source(game):
    """Return (lights by model id, non-light source effect diagnostics)."""
    chunk = game.chunk
    # ResourceImage starts after the 32-byte relocatable-image header.
    model_count = chunk.u32(32 + 24)
    model_ptrs = chunk.u32(32 + 28)
    effect_count = chunk.u32(32 + 52)
    effects_ptr = chunk.u32(32 + 56)
    if model_count > 100_000 or effect_count > 1_000_000:
        raise ValueError('Invalid LCS model/effect count')
    _span(chunk, model_ptrs, model_count * 4, 'model pointer table')
    _span(chunk, effects_ptr, effect_count * _EFFECT_RECORD_SIZE, '2DFX table')

    lights = {}
    non_lights = []
    for model_id in range(model_count):
        model_ptr = chunk.u32(model_ptrs + model_id * 4)
        if not model_ptr:
            continue
        model = game.models.get(model_id)
        if model is None:
            continue
        count = struct.unpack('<b', _span(chunk, model_ptr + 17, 1, 'model 2DFX count'))[0]
        first = struct.unpack('<h', _span(chunk, model_ptr + 24, 2, 'model 2DFX index'))[0]
        if count < 0:
            raise ValueError(f'Model {model_id} has a negative 2DFX count')
        if not count:
            continue
        if first < 0 or first + count > effect_count:
            raise ValueError(f'Model {model_id} has an invalid 2DFX range')
        for local_index in range(count):
            effect_index = first + local_index
            offset = effects_ptr + effect_index * _EFFECT_RECORD_SIZE
            effect_type = _span(chunk, offset + 20, 1, 'effect type')[0]
            if effect_type != _EFFECT_LIGHT:
                non_lights.append(dict(modelId=model_id, effectIndex=effect_index,
                                       sourceType=effect_type))
                continue
            position = struct.unpack('<3f', _span(chunk, offset, 12, 'light position'))
            color = tuple(_span(chunk, offset + 16, 4, 'light color'))
            dist, point_range, corona_size, shadow_size = struct.unpack(
                '<4f', _span(chunk, offset + 24, 16, 'light dimensions'))
            mode, reflection, flare, shadow_intensity, flags = _span(
                chunk, offset + 40, 5, 'light mode/flags')
            corona_ptr, shadow_ptr = struct.unpack(
                '<2I', _span(chunk, offset + 48, 8, 'light texture pointers'))
            if mode >= len(_LIGHT_MODES):
                mode_name = f'unknown_{mode}'
            else:
                mode_name = _LIGHT_MODES[mode]
            source = dict(
                mode=mode, modeName=mode_name, reflection=reflection, flare=flare,
                shadowIntensity=shadow_intensity, flags=flags,
            )
            row = dict(
                modelId=model_id,
                modelName=model.get('name'),
                effectIndex=effect_index,
                position=position,
                color=color,
                lodDistance=dist,
                pointLightRange=point_range,
                coronaSize=corona_size,
                shadowSize=shadow_size,
                coronaTexture=_source_texture(chunk, corona_ptr, 'corona texture'),
                shadowTexture=_source_texture(chunk, shadow_ptr, 'shadow texture'),
                source=source,
                # A coordinator may add this only after proving its SA mapping.
                sa=None,
            )
            lights.setdefault(model_id, []).append(row)
    return lights, non_lights


def effects_by_model(game):
    """Return original EFFECT_LIGHT records keyed by LCS model id.

    Values keep source metadata alongside the translated native light fields.
    """
    lights = _read_source(game)[0]
    for model_id, rows in lights.items():
        if (game.models[model_id].get('name') or '').lower() == 'trafficlight1' and any(row['source']['mode'] == 12 for row in rows):
            rows = trafficlight1_color_by_index(rows)
            lights[model_id] = rows
        for row in rows:
            row['sa'] = native_light_mapping(row)
    return lights


def trafficlight1_color_by_index(effects):
    """Copy the six trafficlight1 anchors with SA-readable color classes.

    SA's native ``CTrafficLights::DisplayActualLight`` classifies each 2DFX
    color as green, yellow or red, then selects the current phase and face.
    LCS stores these six records white and derives phase color from the
    vertical tier: low green, middle yellow, high red.  The returned copies
    preserve the original ``sourceColor`` and positions; color alpha is kept
    from the source because SA's traffic-light consumer supplies corona alpha
    itself.  This only translates the color classifier; it does not translate
    SA day/night flags or flashiness.
    """
    effects = list(effects)
    if len(effects) != 6 or any(
            row.get('source', {}).get('mode') != 12 for row in effects):
        raise ValueError('Expected six LCS LIGHT_SPECIAL trafficlight1 effects')
    positions = [tuple(row['position']) for row in effects]
    if any(len(point) != 3 for point in positions):
        raise ValueError('Traffic-light positions must contain three coordinates')
    ys = sorted({point[1] for point in positions})
    zs = sorted({point[2] for point in positions})
    if len(ys) != 2 or len(zs) != 3 or len(set((p[1], p[2]) for p in positions)) != 6:
        raise ValueError('Traffic-light 2DFX points do not form six y/z face-tier pairs')
    colors = ((0, 255, 0), (255, 128, 0), (255, 0, 0))
    tier_color = dict(zip(zs, colors))
    converted = []
    for row, point in zip(effects, positions):
        original = tuple(row['color'])
        if len(original) != 4:
            raise ValueError('Traffic-light source color must contain RGBA')
        converted.append(dict(row, sourceColor=original,
                              color=(*tier_color[point[2]], original[3])))
    return converted


def native_light_mapping(effect):
    """Choose native SA light behavior, never reinterpret a Leeds mode byte.

    Retail ProcessLightsForEntity reads day/night from flags 0x20/0x40 at
    effect+0x24 (6FCA9A..6FCABE), and flashiness from effect+0x26 (6FCB09).
    The special traffic-light points have no generic emitter: PreRender's
    owned adapter delegates them to CTrafficLights::DisplayActualLight.
    """
    source = effect['source']
    mode = source['mode']
    flags = source.get('flags', 0)
    if flags & ~0x07:
        raise ValueError('Unsupported original light flags (including hide-object)')
    if mode == 12:
        if (effect.get('modelName') or '').lower() != 'trafficlight1':
            raise ValueError('Unowned special light mode')
        return dict(flags=0, flashiness=0)
    flashiness = {0: 0, 1: 0, 2: 1, 3: 1, 4: 3, 5: 3,
                  6: 4, 7: 4, 8: 5, 9: 5, 10: 6, 11: 6,
                  13: 3}.get(mode)
    if flashiness is None:
        raise ValueError('Unsupported original light mode')
    temporal_flags = 0x40 if mode in (1, 3, 5, 7, 9, 11) else 0x60
    return dict(flags=flags | temporal_flags, flashiness=flashiness)


def _finite_float(value, name):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError(f'Non-finite SA 2DFX {name}')
    return value


def _byte(value, name):
    value = int(value)
    if not 0 <= value <= 255:
        raise ValueError(f'SA 2DFX {name} is outside byte range')
    return value


def _texture_bytes(value, name):
    if not isinstance(value, str):
        raise ValueError(f'SA 2DFX {name} must be a string')
    encoded = value.encode('ascii')
    if len(encoded) > 23 or b'\0' in encoded:
        raise ValueError(f'SA 2DFX {name} must fit 23 ASCII bytes')
    return encoded + bytes(24 - len(encoded))


def _effect_payload(effects):
    if len(effects) > 255:
        raise ValueError('SA Geometry 2DFX effect count exceeds 255')
    payload = bytearray(struct.pack('<I', len(effects)))
    for effect in effects:
        sa = effect.get('sa')
        if not isinstance(sa, dict):
            raise ValueError(
                'LCS light has no proven SA temporal flags/flashiness mapping')
        if 'flags' not in sa or 'flashiness' not in sa:
            raise ValueError('SA light mapping must supply flags and flashiness')
        flags = int(sa['flags'])
        if not 0 <= flags <= 0xFFFF:
            raise ValueError('SA 2DFX flags are outside uint16 range')
        light_dir = tuple(sa.get('lightDirection', (0, 0, 100)))
        if len(light_dir) != 3 or any(not -128 <= int(v) <= 127 for v in light_dir):
            raise ValueError('SA light direction must contain three signed bytes')
        color = tuple(int(v) for v in effect['color'])
        if len(color) != 4 or any(not 0 <= v <= 255 for v in color):
            raise ValueError('LCS light color must contain four bytes')
        position = tuple(_finite_float(v, 'position') for v in effect['position'])
        if len(position) != 3:
            raise ValueError('LCS light position must contain three floats')
        body = _SA_LIGHT_ATTR.pack(
            *color,
            _finite_float(effect['lodDistance'], 'LOD distance'),
            _finite_float(effect['pointLightRange'], 'point-light range'),
            _finite_float(effect['coronaSize'], 'corona size'),
            _finite_float(effect['shadowSize'], 'shadow size'),
            _byte(sa['flashiness'], 'flashiness'),
            _byte(sa.get('reflectionType', effect['source']['reflection']), 'reflection type'),
            _byte(sa.get('lensFlareType', effect['source']['flare']), 'lens flare type'),
            _byte(sa.get('shadowAlpha', effect['source']['shadowIntensity']), 'shadow alpha'),
            flags & 0xFF,
            _texture_bytes(effect['coronaTexture'], 'corona texture'),
            _texture_bytes(effect['shadowTexture'], 'shadow texture'),
            _byte(sa.get('shadowDepth', 0), 'shadow depth'),
            (flags >> 8) & 0xFF,
            *(int(v) for v in light_dir),
        )
        payload += _EFFECT_HEADER.pack(*position, _EFFECT_LIGHT, len(body))
        payload += body
    return bytes(payload)


def _rw_chunk(ident, version, body):
    return struct.pack('<3I', ident, len(body), version) + body


def _walk(nodes):
    for node in nodes:
        yield node
        yield from _walk(node.get('kids', ()))


def _rewrite_node(raw, node, target_offset, replacement):
    if node['off'] == target_offset:
        return replacement
    children = node.get('kids')
    if not children:
        return raw[node['off']:node['body'][1]]
    start, stop = node['body']
    out = bytearray()
    cursor = start
    changed = False
    for child in children:
        child_end = child['body'][1]
        if child['off'] < start or child_end > stop:
            raise ValueError('Invalid nested RenderWare chunk bounds')
        out += raw[cursor:child['off']]
        if child['off'] <= target_offset < child_end:
            out += _rewrite_node(raw, child, target_offset, replacement)
            changed = True
        else:
            out += raw[child['off']:child_end]
        cursor = child_end
    out += raw[cursor:stop]
    if not changed:
        return raw[node['off']:stop]
    return _rw_chunk(node['id'], node['ver'], bytes(out))


def inject_dff(data, effects):
    """Append mapped lights to the first Geometry's SA 2DFX plugin.

    Existing 2DFX plugin chunks are preserved.  An identical payload is
    idempotent; a different existing payload is a conflict and raises rather
    than replacing effects authored in the converted DFF.
    """
    effects = list(effects)
    if not effects:
        return data
    plugin_payload = _effect_payload(effects)
    if len(data) < 12:
        raise ValueError('Truncated RenderWare DFF')
    root_len = struct.unpack_from('<I', data, 4)[0] + 12
    if root_len > len(data) or any(data[root_len:]):
        raise ValueError('Invalid RenderWare DFF bounds or nonzero padding')
    root_nodes = parse_rw(data, 0, root_len)
    if len(root_nodes) != 1 or root_nodes[0]['id'] != 16:
        raise ValueError('Expected one bounded RenderWare Clump')
    root = root_nodes[0]
    geometries = [node for node in _walk([root]) if node['id'] == 15]
    if not geometries:
        raise ValueError('DFF has no RenderWare Geometry')

    existing = []
    for geometry in geometries:
        for ext in (node for node in geometry.get('kids', ()) if node['id'] == 3):
            for child in ext.get('kids', ()):
                if child['id'] == SA_2DFX_PLUGIN:
                    a, b = child['body']
                    existing.append(data[a:b])
    if existing:
        if len(existing) == 1 and existing[0] == plugin_payload:
            return data
        raise ValueError('DFF already contains a different SA 2DFX plugin; preserving it')

    target_geometry = geometries[0]
    extensions = [node for node in target_geometry.get('kids', ()) if node['id'] == 3]
    if extensions:
        ext = extensions[0]
        a, b = ext['body']
        replacement = _rw_chunk(3, ext['ver'], data[a:b] +
                                _rw_chunk(SA_2DFX_PLUGIN, ext['ver'], plugin_payload))
        rewritten = _rewrite_node(data, root, ext['off'], replacement)
    else:
        a, b = target_geometry['body']
        extension = _rw_chunk(3, target_geometry['ver'],
                              _rw_chunk(SA_2DFX_PLUGIN, target_geometry['ver'], plugin_payload))
        replacement = _rw_chunk(target_geometry['id'], target_geometry['ver'],
                                data[a:b] + extension)
        rewritten = _rewrite_node(data, root, target_geometry['off'], replacement)
    return rewritten + data[root_len:]


def export_assets(game, models, assets, output_report=None, light_mapper=None):
    """Inject supported lights into principal generated DFFs and report gaps.

    ``models`` is the converted principal-model map keyed by original model
    id; render-layer rows are not accepted as destinations. ``assets`` is its
    private DFF directory. ``light_mapper(effect)`` may return the proven SA
    mapping stored under ``effect['sa']``. The returned report contains counts
    and reasons, never source coordinates or extracted effect tables.
    """
    lights = effects_by_model(game)
    _, non_lights = _read_source(game)
    directory = Path(assets)
    result = dict(format=1, sourceModelsWithLights=len(lights),
                  sourceLights=sum(len(rows) for rows in lights.values()),
                  injectedModels=0, injectedLights=0, unsupportedLights=0,
                  unsupportedNonLightEffects=len(non_lights), missingAssets=[],
                  unmappedSourceModels=[], nonLightEffects=non_lights,
                  models=[])
    for model_id, rows in sorted(lights.items()):
        model = models.get(model_id)
        if not model:
            result['unmappedSourceModels'].append(model_id)
            result['unsupportedLights'] += len(rows)
            continue
        source_name = game.models[model_id].get('name') or ''
        target_name = model.get('name') or ''
        if (model.get('primary') is False or not source_name or
                target_name.lower() != source_name.lower()):
            result['unmappedSourceModels'].append(model_id)
            result['unsupportedLights'] += len(rows)
            continue
        path = directory / (target_name + '.dff')
        if not path.is_file():
            result['missingAssets'].append(dict(modelId=model_id, name=target_name))
            result['unsupportedLights'] += len(rows)
            continue
        ready = []
        for source in rows:
            effect = dict(source)
            reason = 'SA day/night flags and flashiness mapping not supplied'
            if effect['sa'] is None and light_mapper is not None:
                try:
                    mapped = light_mapper(dict(source))
                except (KeyError, ValueError, NotImplementedError) as error:
                    mapped = None
                    reason = str(error) or 'light mapper did not support source mode'
                else:
                    reason = 'SA day/night flags and flashiness mapping not supplied'
                if mapped is not None:
                    effect['sa'] = dict(mapped)
            if effect['sa'] is None:
                result['unsupportedLights'] += 1
                result['models'].append(dict(modelId=model_id, name=target_name,
                    effectIndex=source['effectIndex'], sourceMode=source['source']['mode'],
                    reason=reason))
            else:
                ready.append(effect)
        if not ready:
            continue
        original = path.read_bytes()
        converted = inject_dff(original, ready)
        if converted != original:
            path.write_bytes(converted)
        result['injectedModels'] += 1
        result['injectedLights'] += len(ready)
    if output_report is not None:
        report_path = Path(output_report)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result

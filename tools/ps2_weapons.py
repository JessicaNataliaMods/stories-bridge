"""Export the original PS2 weapon-model definitions as separate SA RW assets.

The caller supplies the user's extracted LCS PS2 files. This module contains no
game assets and does not assign SA weapon slots or install any output.
"""
import collections
import hashlib
import importlib.util
import json
import pathlib
import re
import struct

from ps2_player import run
from ps2_world import Chunk, Game


WEAPON_MODEL_TYPE = 4


def load_texture_audit(root):
    spec = importlib.util.spec_from_file_location(
        'weapon_texture_audit', root / 'tools/validate-ps2-texture-pixels.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _model_info(text):
    """Read converter's bounded geometry summary without dropping atomics."""
    atomics = []
    textures = set()
    skinned = None
    for line in text.splitlines():
        match = re.fullmatch(r'atomic=(.*?) vertices=(\d+)', line)
        if match:
            atomics.append(dict(name=match.group(1), vertices=int(match.group(2))))
            continue
        match = re.fullmatch(r'atomic_bounds name=(.*?) min=([^ ]+) max=(.*)', line)
        if match:
            name, low, high = match.groups()
            bounds = tuple(float(value) for value in (low + ',' + high).split(','))
            if len(bounds) != 6:
                raise ValueError('Invalid model bounds in converter info')
            atomics.append(dict(boundsName=name, bounds=bounds))
            continue
        if line.startswith('texture='):
            textures.add(line[8:])
        elif line.startswith('skinned='):
            skinned = line[8:] == 'true'
    geometry = [row for row in atomics if 'vertices' in row]
    bounds = [row for row in atomics if 'bounds' in row]
    if not geometry or skinned is None:
        raise ValueError('Converter did not report model geometry')
    return dict(atomics=geometry, bounds=bounds, textures=sorted(textures), skinned=skinned)


def _compare_model_info(source, final, label):
    before, after = _model_info(source), _model_info(final)
    if before['atomics'] != after['atomics']:
        raise ValueError(label + ': atomic names or vertex counts changed')
    if before['textures'] != after['textures']:
        raise ValueError(label + ': material texture references changed')
    if before['skinned'] != after['skinned'] or before['skinned']:
        raise ValueError(label + ': unexpected skinning on a weapon model')
    # Converter info prints bounds only for decoded, non-native geometry. When
    # both sides report them, ensure conversion retained each atomic's extent.
    if before['bounds'] and after['bounds']:
        if len(before['bounds']) != len(after['bounds']):
            raise ValueError(label + ': atomic bounds count changed')
        for left, right in zip(before['bounds'], after['bounds']):
            if left['boundsName'] != right['boundsName'] or any(
                    abs(a - b) > 1e-5 for a, b in zip(left['bounds'], right['bounds'])):
                raise ValueError(label + ': atomic bounds changed')
    return dict(atomics=before['atomics'], boundsCompared=bool(before['bounds'] and after['bounds']),
                textureReferences=before['textures'], atomicStructurePreserved=True,
                textureReferencesPreserved=True, unskinned=True)


def _asset_names(candidates):
    counts = collections.Counter(row['originalName'].lower() for row in candidates if row['originalName'])
    used = set()
    for row in candidates:
        name = row['originalName']
        if not name:
            row['asset'] = None
            continue
        stem = re.sub(r'[^a-z0-9_]+', '_', name.lower()).strip('_') or 'model'
        asset = 'lcs_' + stem
        if counts[name.lower()] > 1 or asset.lower() in used:
            asset += '_m' + str(row['originalModelId'])
        if len((asset + '.dff').encode('ascii')) > 23:
            raise ValueError('Weapon output name exceeds SA IMG filename capacity: ' + name)
        if asset.lower() in used:
            raise ValueError('Cannot give duplicate source weapon definitions unique output names')
        row['asset'] = asset
        used.add(asset.lower())


def _texture_digest(image):
    width, height, pixels = image
    return hashlib.sha256(struct.pack('<2I', width, height) + pixels).hexdigest()


def _write_pst(path, images):
    with path.open('wb') as stream:
        stream.write(struct.pack('<4sI', b'PST1', len(images)))
        for name, (width, height, pixels) in sorted(images.items()):
            encoded = name.encode('ascii')
            if not 0 < len(encoded) <= 31:
                raise ValueError('Original texture name does not fit SA: ' + name)
            if (not width or not height or width > 2048 or height > 2048 or
                    len(pixels) != width * height * 4):
                raise ValueError('Invalid original weapon texture: ' + name)
            stream.write(encoded.ljust(32, b'\0'))
            stream.write(struct.pack('<2I', width, height))
            stream.write(pixels)


def export_weapons(rom, output, root):
    """Convert every streamed type-4 PS2 model to separate named DFF/TXD files."""
    rom, output, root = map(pathlib.Path, (rom, output, root))
    if output.exists():
        raise ValueError('Weapon output exists; choose a new output')
    output.mkdir(parents=True)
    intermediate, assets, logs = (output / name for name in ('intermediate', 'rw-assets', 'logs'))
    for directory in (intermediate, assets, logs):
        directory.mkdir()

    game = Game(rom, root / 'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    candidates = []
    type_counts = collections.Counter()
    for model_id, source in sorted(game.models.items()):
        type_counts[source['type']] += 1
        if source['type'] != WEAPON_MODEL_TYPE:
            continue
        candidates.append(dict(originalModelId=model_id, originalName=source['name'],
                               originalHash=source['hash'], modelType=source['type'],
                               sourceDictionary=source['txd'], sourceTextureSlot=source['txdSlot'],
                               sourceOffset=source['streamOffset'], sourceSize=source['streamSize']))
    if not candidates:
        raise ValueError('No original type-4 weapon model definitions found')
    _asset_names(candidates)
    game_file = intermediate / 'game.dat'
    game_file.write_bytes(game.chunk.data)
    img_path = rom / 'MODELS/GTA3PS2.IMG'
    img_size = img_path.stat().st_size
    rsl = root / 'build/Release/lcs-ps2-rsl.exe'
    rw = root / 'build/Release/lcs-convert-rw.exe'
    writer = root / 'build/Release/lcs-ps2-write.exe'

    # Retain source dictionaries and a global name index for safe,
    # pixel-identical texture fallbacks.
    dictionaries = collections.defaultdict(dict)
    by_name = collections.defaultdict(list)
    for texture in game.named_textures():
        dictionary, name, image = texture['dictionary'].lower(), texture['name'], texture['image']
        if not name:
            raise ValueError('Original weapon texture has an empty name')
        previous = dictionaries[dictionary].get(name)
        if previous is not None and previous != image:
            raise ValueError('Conflicting original texture identity: ' + dictionary + ':' + name)
        dictionaries[dictionary][name] = image
        by_name[name].append((dictionary, image))

    rows, failures, expected_textures = [], [], {}
    with img_path.open('rb') as stream:
        for row in candidates:
            label = row['originalName'] or ('model ' + str(row['originalModelId']))
            row_log = [f"source model={label} id={row['originalModelId']} type={row['modelType']}"]
            try:
                if not row['asset'] or not row['sourceDictionary']:
                    raise ValueError('Unresolved original weapon name or TXD dictionary')
                pos, size = row['sourceOffset'], row['sourceSize']
                if pos < 0 or size < 32 or pos + size > img_size:
                    raise ValueError('Original weapon stream outside GTA3PS2.IMG')
                stream.seek(pos)
                data = stream.read(size)
                if len(data) != size:
                    raise ValueError('Truncated original weapon stream')
                length = struct.unpack_from('<I', data, 8)[0]
                if not 32 <= length <= size:
                    raise ValueError('MDL size exceeds original weapon stream')
                chunk = Chunk(data[:length], label)
                if chunk.u32(0) != 0x6d646c:
                    raise ValueError('Expected original PS2 MDL')
                row['sourceModelSHA256'] = hashlib.sha256(chunk.data).hexdigest()
                mdl = intermediate / (row['asset'] + '.mdl')
                raw = intermediate / (row['asset'] + '-source.dff')
                mdl.write_bytes(chunk.data)
                row_log.append(run(rsl, '-g', game_file, mdl, raw).strip())
                source_info = run(rw, raw, '--info')
                final = assets / (row['asset'] + '.dff')
                row['conversion'] = run(rw, raw, final).strip()
                final_info = run(rw, final, '--info')
                row['geometryValidation'] = _compare_model_info(source_info, final_info, label)
                row['sourceDffSHA256'] = hashlib.sha256(raw.read_bytes()).hexdigest()
                row['sourceInfoSHA256'] = hashlib.sha256(source_info.encode('utf-8')).hexdigest()
                row['dffSHA256'] = hashlib.sha256(final.read_bytes()).hexdigest()
                row['donorSaSlot'] = None
                row['installed'] = False
                row_log.extend(('source --info:\n' + source_info, 'final --info:\n' + final_info,
                                'conversion: ' + row['conversion']))
                rows.append(row)
            except (OSError, ValueError, RuntimeError, struct.error) as error:
                failures.append(dict(row, error=str(error)))
                row_log.append('ERROR: ' + str(error))
            (logs / ((row['asset'] or ('model-' + str(row['originalModelId']))) + '.model.log')).write_text(
                '\n'.join(row_log) + '\n', encoding='utf-8')

    texture_rows = []
    for row in rows:
        label = row['originalName']
        log_path = logs / (row['asset'] + '.model.log')
        try:
            required = row['geometryValidation']['textureReferences']
            preferred = row['sourceDictionary'].lower()
            chosen = dict(dictionaries.get(preferred, {}))
            sources = {name: [preferred] for name in chosen}
            for name in required:
                if name in chosen:
                    continue
                options = by_name.get(name, [])
                if not options or any(image != options[0][1] for _dictionary, image in options):
                    raise ValueError('Missing or ambiguous original texture: ' + name)
                chosen[name] = options[0][1]
                sources[name] = sorted({dictionary for dictionary, _image in options})
            pst = intermediate / (row['asset'] + '.pst')
            _write_pst(pst, chosen)
            expected_textures[row['asset']] = {
                name: (width, height, pixels, any(alpha != 255 for alpha in pixels[3::4]))
                for name, (width, height, pixels) in chosen.items()}
            row['sourceTextureDictionaries'] = sources
            row['sourceTextureSHA256'] = {name: _texture_digest(image) for name, image in chosen.items()}
            row['requiredTextures'] = required
            texture_rows.append(row)
            with log_path.open('a', encoding='utf-8') as log:
                log.write('texture source dictionaries: ' + json.dumps(sources, sort_keys=True) + '\n')
                log.write('source PST: ' + str(pst.name) + '\n')
        except (OSError, ValueError, RuntimeError) as error:
            failures.append(dict(row, error='texture export: ' + str(error)))
            with log_path.open('a', encoding='utf-8') as log:
                log.write('ERROR: texture export: ' + str(error) + '\n')

    native_textures = intermediate / 'native-textures'
    if texture_rows:
        try:
            texture_log = run(writer, intermediate, native_textures)
            (logs / 'texture-writer.log').write_text(texture_log, encoding='utf-8')
        except (OSError, RuntimeError) as error:
            failures.append(dict(stage='texture-writer', error=str(error)))
            (logs / 'texture-writer.log').write_text(str(error), encoding='utf-8')
            texture_rows = []

    audit = load_texture_audit(root)
    converted = []
    for row in texture_rows:
        log_path = logs / (row['asset'] + '.model.log')
        try:
            source_txd = native_textures / (row['asset'] + '.txd')
            if not source_txd.is_file():
                raise ValueError('Texture writer omitted source dictionary')
            txd = assets / (row['asset'] + '.txd')
            # lcs-ps2-write emits the native D3D9 TXD format used by the
            # existing ped exporter, so retain its pixels without a second
            # RenderWare rewrite.
            txd.write_bytes(source_txd.read_bytes())
            actual = audit.native_textures(txd)
            if actual != expected_textures[row['asset']]:
                raise ValueError('Original texture names, pixels, or alpha changed')
            row.update(textures=len(actual), txdSHA256=hashlib.sha256(txd.read_bytes()).hexdigest(),
                       texturePixelsNamesAlphaVerified=True, donorMapping='unassigned',
                       runtimeVerified=False)
            with log_path.open('a', encoding='utf-8') as log:
                log.write('texture TXD copied from native D3D9 writer output\n')
                log.write('texture pixels, names, and alpha verified by native TXD reread\n')
            converted.append(row)
        except (OSError, ValueError, RuntimeError) as error:
            failures.append(dict(row, error='texture conversion: ' + str(error)))
            with log_path.open('a', encoding='utf-8') as log:
                log.write('ERROR: texture conversion: ' + str(error) + '\n')

    actor_validation = None
    texture_validation = None
    if not failures:
        try:
            actor_validation = run(rw, assets, '--validate-actors').strip()
            texture_validation = audit.validate(output)
        except (OSError, ValueError, RuntimeError) as error:
            failures.append(dict(stage='final-validation', error=str(error)))

    report = dict(format=1, mode='WeaponModelsOnly', modelType=WEAPON_MODEL_TYPE,
                  modelTypeCounts={str(key): value for key, value in sorted(type_counts.items())},
                  candidateCount=len(candidates), converted=converted, failures=failures,
                  actorRereadValidation=actor_validation, texturePixelValidation=texture_validation,
                  sourceDefinitionsPreservedSeparately=True, donorSaSlotsAssigned=False,
                  installed=False, gameExecuted=False, runtimeVerified=False,
                  complete=(not failures and len(converted) == len(candidates)))
    (output / 'manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if failures:
        raise ValueError(str(len(failures)) + ' weapon export/validation failures; see manifest.json')
    return report

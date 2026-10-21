"""Batch-export all PS2 gameplay peds and classify extra gameplay characters.

No installation or game launch. Source data and skeletons come from the user's
games; every final mesh and dictionary is audited separately against its input.
"""
import collections
import hashlib
import importlib.util
import json
import pathlib
import struct
from ps2_world import Game, Chunk
from ps2_player import run, extract_native_ped

PED_TYPES = ('PLAYER1', 'PLAYER2', 'PLAYER3', 'PLAYER4', 'CIVMALE', 'CIVFEMALE',
             'COP', 'GANG1', 'GANG2', 'GANG3', 'GANG4', 'GANG5', 'GANG6', 'GANG7',
             'GANG8', 'GANG9', 'MEDIC', 'FIREMAN', 'CRIMINAL', 'SPECIAL', 'PROSTITUTE')
COLOR_MARKERS = {(0, 255, 60), (175, 0, 255), (255, 255, 0), (255, 0, 255)}
# Original named gameplay identities/variants: campaign woman, Toshiko in
# kimono/western dress (including the duplicate zzzzz resource), and Maria.
FEMALE_EXTRAS = {'campwom', 'kas_01', 'wkas_01', 'zzzzz', 'mar_01'}


def load_audit(root):
    spec = importlib.util.spec_from_file_location('ped_audit', root / 'tools/validate-ps2-player.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inventory(game):
    rows, excluded = [], []
    c = game.chunk
    pointers = c.u32(32 + 28)
    for model, row in sorted(game.models.items()):
        if row['type'] != 7:
            continue
        if not row['streamSize']:
            excluded.append(dict(originalModelId=model, originalName=row['name'], reason='no streamed model (player/special placeholder)'))
            continue
        if not row['name'] or not row['txd']:
            raise ValueError('Unresolved original ped identity')
        pointer = c.u32(pointers + 4 * model)
        anim, ped_type, stats, cars = c.read('<4I', pointer + 44)
        if ped_type >= len(PED_TYPES):
            raise ValueError('Unknown original ped type')
        rows.append(dict(originalModelId=model, originalName=row['name'], sourceDictionary=row['txd'],
                         sourceKind='population', sourceOffset=row['streamOffset'], sourceSize=row['streamSize'],
                         pedType=PED_TYPES[ped_type], originalPedType=ped_type,
                         originalAnimGroup=anim, originalPedStats=stats, originalCarsMask=cars))
    directory = c.u32(game.streaming + 0x1e328)
    entries, capacity, count = c.read('<3I', directory)
    if not 0 < count <= capacity <= 1000:
        raise ValueError('Invalid PS2 extra directory')
    extras = []
    for offset, size, name_bytes in c.read_array('<II24s', entries, count):
        if b'\0' not in name_bytes:
            raise ValueError('Unterminated PS2 extra name')
        name = name_bytes.split(b'\0')[0].decode('ascii')
        if name.lower().startswith(('cs', 'player', 'plr', 'toni_')):
            excluded.append(dict(originalName=name, sourceOffset=offset * 2048, reason='cutscene/player resource: separate conversion stage'))
            continue
        if not size:
            raise ValueError('Empty gameplay extra stream')
        extras.append(dict(originalModelId=None, originalName=name, sourceDictionary=name,
                           sourceKind='extraGameplay', sourceOffset=offset * 2048, sourceSize=size * 2048,
                           pedType='SPECIAL', sourceGender='not declared in extra directory'))
    counts = collections.Counter(row['originalName'].lower() for row in rows + extras)
    occurrences = collections.Counter()
    for row in rows + extras:
        name = row['originalName'].lower()
        occurrences[name] += 1
        row['asset'] = 'lcs_' + name + ('_' + str(occurrences[name]) if counts[name] > 1 else '')
        if len((row['asset'] + '.dff').encode('ascii')) > 23:
            raise ValueError('Ped output name exceeds IMG filename capacity')
    return rows + extras, excluded


def material_colors(path, audit):
    raw = path.read_bytes()
    root = next(audit.chunks(raw, 0, len(raw)))
    geometries = audit.child(raw, root, 0x1a)
    colors = []
    for geometry in audit.chunks(raw, geometries[1], geometries[2]):
        if geometry[0] != 0x0f:
            continue
        materials = audit.child(raw, geometry, 8)
        for material in audit.chunks(raw, materials[1], materials[2]):
            if material[0] == 7:
                data = audit.child(raw, material, 1)
                colors.append(list(raw[data[1] + 4:data[1] + 8]))
    return colors


def audit_mesh(original, final, reference, label, audit):
    source, native, donor = (audit.mesh(path) for path in (original, final, reference))
    for key in ('nv', 'nt', 'colors', 'uv', 'triangles', 'weights'):
        if source[key] != native[key]:
            raise ValueError(label + ': source ' + key + ' changed')
    from ped_rig_audit import verify
    rig = verify(original, final, reference, audit)
    for i, (weight,) in enumerate(struct.iter_unpack('<f', source['weights'])):
        if weight and native['bones'][native['indices'][i]] != {54: 44, 55: 54, 2000: 44, 2001: 54}.get(source['bones'][source['indices'][i]], source['bones'][source['indices'][i]]):
            raise ValueError(label + ': weighted bone identity changed')
    if material_colors(original, audit) != material_colors(final, audit):
        raise ValueError(label + ': material colors changed')
    return dict(vertices=native['nv'], triangles=native['nt'], nativeBones=len(native['bones']),
                bounds=native['bounds'], topologyUVPrelightWeightsPreserved=True, bindMatricesVerified=True,rigAudit=rig)


def export_peds(rom, sa_game, output, root):
    if output.exists():
        raise ValueError('Ped output exists; choose a new output')
    output.mkdir(parents=True)
    intermediate, assets = output / 'intermediate', output / 'rw-assets'
    intermediate.mkdir()
    assets.mkdir()
    game = Game(rom, root / 'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    candidates, excluded = inventory(game)
    audit = load_audit(root)
    references = {}
    for name in ('male01', 'bfyst'):
        path = intermediate / ('native-' + name + '.dff')
        extract_native_ped(sa_game / 'models/gta3.img', path, name)
        references[name] = path
    game_file = intermediate / 'game.dat'
    game_file.write_bytes(game.chunk.data)
    rsl, rw = (root / 'build/Release' / name for name in ('lcs-ps2-rsl.exe', 'lcs-convert-rw.exe'))
    rows, failures = [], []
    with (rom / 'MODELS/GTA3PS2.IMG').open('rb') as stream:
        for row in candidates:
            try:
                pos, size = row['sourceOffset'], row['sourceSize']
                if pos < 0 or size < 32 or pos + size > (rom / 'MODELS/GTA3PS2.IMG').stat().st_size:
                    raise ValueError('Original ped stream outside IMG')
                stream.seek(pos)
                data = stream.read(size)
                length = struct.unpack_from('<I', data, 8)[0]
                if not 32 <= length <= size:
                    raise ValueError('MDL size exceeds original stream')
                chunk = Chunk(data[:length], row['originalName'])
                if chunk.u32(0) != 0x6d646c:
                    raise ValueError('Expected original MDL')
                row['sourceSHA256'] = hashlib.sha256(chunk.data).hexdigest()
                mdl, raw = (intermediate / (row['asset'] + extension) for extension in ('.mdl', '.dff'))
                mdl.write_bytes(chunk.data)
                run(rsl, '-g', game_file, mdl, raw)
                info = run(rw, raw, '--info')
                if 'skinned=true' not in info or 'bones=24' not in info or info.count('atomic=') != 1:
                    if row['sourceKind'] == 'population':
                        raise ValueError('Unexpected population ped layout')
                    excluded.append(dict(row, reason='extra is not a single 24-bone gameplay ped', modelInfo=info))
                    continue
                row['requiredTextures'] = sorted({line[8:] for line in info.splitlines() if line.startswith('texture=')})
                if not row['requiredTextures']:
                    raise ValueError('Gameplay ped has no named textures')
                reference = 'bfyst' if row['pedType'] in ('CIVFEMALE', 'PROSTITUTE') or row['originalName'].lower() in FEMALE_EXTRAS else 'male01'
                row['nativeReference'] = reference
                row['nativeReferenceSHA256'] = hashlib.sha256(references[reference].read_bytes()).hexdigest()
                final = assets / (row['asset'] + '.dff')
                row['conversion'] = run(rw, raw, final, '--sa-ped', references[reference]).strip()
                generic = intermediate / (row['asset'] + '-audit.dff')
                run(rw, raw, generic)
                row['meshAudit'] = audit_mesh(generic, final, references[reference], row['asset'], audit)
                row['materialColors'] = material_colors(generic, audit)
                row['pedColorMarkers'] = [color for color in row['materialColors'] if tuple(color[:3]) in COLOR_MARKERS]
                rows.append(row)
            except (ValueError, RuntimeError) as error:
                failures.append(dict(row, error=str(error)))
            if (len(rows) + len(failures)) % 20 == 0:
                print('Ped mesh progress:', len(rows), 'converted,', len(failures), 'errors', flush=True)
    # Keep ambiguous texture identities separate. A fallback is accepted only
    # if all matching original dictionaries contain exactly identical pixels.
    dictionaries, by_name = {}, collections.defaultdict(list)
    for texture in game.named_textures():
        dictionary, name, image = texture['dictionary'].lower(), texture['name'], texture['image']
        previous = dictionaries.setdefault(dictionary, {}).get(name)
        if previous is not None and previous != image:
            raise ValueError('Conflicting original internal texture identity')
        dictionaries[dictionary][name] = image
        by_name[name].append((dictionary, image))
    inputs, tex_output = intermediate / 'textures', intermediate / 'native-textures'
    inputs.mkdir()
    for row in rows:
        try:
            chosen, sources = {}, {}
            preferred = row['sourceDictionary'].lower()
            for name in row['requiredTextures']:
                if name in dictionaries.get(preferred, {}):
                    chosen[name], sources[name] = dictionaries[preferred][name], [preferred]
                else:
                    choices = by_name[name]
                    if not choices or any(image != choices[0][1] for _, image in choices):
                        raise ValueError('Missing/ambiguous texture: ' + name)
                    chosen[name], sources[name] = choices[0][1], sorted({d for d, _ in choices})
            with (inputs / (row['asset'] + '.pst')).open('wb') as stream:
                stream.write(struct.pack('<4sI', b'PST1', len(chosen)))
                for name, (w, h, pixels) in sorted(chosen.items()):
                    if not 0 < len(name.encode('ascii')) <= 31:
                        raise ValueError('Internal texture name does not fit SA')
                    stream.write(name.encode('ascii').ljust(32, b'\0') + struct.pack('<2I', w, h) + pixels)
            row['textureSourceDictionaries'] = sources
            row['_expectedTextures'] = {name: (w, h, data, any(a != 255 for a in data[3::4])) for name, (w, h, data) in chosen.items()}
        except ValueError as error:
            failures.append(dict(row, error=str(error)))
    run(root / 'build/Release/lcs-ps2-write.exe', inputs, tex_output)
    finished = []
    for row in rows:
        if '_expectedTextures' not in row:
            continue
        txd = assets / (row['asset'] + '.txd')
        txd.write_bytes((tex_output / txd.name).read_bytes())
        actual = audit._textures.native_textures(txd)
        expected = row.pop('_expectedTextures')
        if actual != expected:
            raise ValueError('Original texture pixels/names/alpha changed: ' + row['asset'])
        row.update(texturePixelsNamesAlphaVerified=True, textures=len(actual),
                   dffSHA256=hashlib.sha256((assets / (row['asset'] + '.dff')).read_bytes()).hexdigest(),
                   txdSHA256=hashlib.sha256(txd.read_bytes()).hexdigest(), installed=False, runtimeVerified=False)
        finished.append(row)
    validation = run(rw, assets, '--validate-actors').strip() if not failures else None
    report = dict(format=1, mode='GameplayPedsOnly', gameExecuted=False, installed=False, actorRereadValidation=validation,
                  populationSourceCount=sum(r['sourceKind'] == 'population' for r in candidates),
                  extraCandidateCount=sum(r['sourceKind'] == 'extraGameplay' for r in candidates),
                  converted=finished, excluded=excluded, failures=failures,
                  allStreamedPopulationConverted=sum(r['sourceKind'] == 'population' for r in finished) == sum(r['sourceKind'] == 'population' for r in candidates),
                  pedColorMarkersFound=sum(len(r['pedColorMarkers']) for r in finished),
                  complete=not failures, playerImgModified=False)
    (output / 'manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if failures:
        raise ValueError(str(len(failures)) + ' ped conversion failures; see manifest.json')
    return report

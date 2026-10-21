"""Build a private Modloader map preview from user-converted assets.

Does not launch the game or write to its installation. The public converter
distribution must contain this code only, never this tool's generated media.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import shutil
import struct
import subprocess
from traffic_profile import mount_navigation
from native_collisions import mount as mount_native_collisions
from bootstrap_player_exports import export_player_globals


SECTIONS = {'objs', 'tobj', 'weap', 'hier', 'anim', 'cars', 'peds'}
GENERIC = ['vegepart', 'barriers', 'dynamic', 'dynamic2', 'multiobj', 'procobj']
from ps2_export import PHYSICAL_DOOR_MODEL_NAMES


def definitions(path):
    section = ''
    for line in path.read_text(encoding='ascii', errors='strict').splitlines():
        line = line.split('#')[0].strip()
        if not line:
            continue
        if line == 'end':
            section = ''
        elif ',' not in line:
            section = line
        elif section in SECTIONS:
            yield section, [field.strip() for field in line.split(',')]


def profile_definitions(game, profiles, replacement=None):
    result = []
    for profile in profiles:
        for line in profile.read_text(encoding='ascii').splitlines():
            match = re.match(r'^IDE\s+([^#]+)', line, re.I)
            if match:
                relative = match[1].strip().replace('\\', '/')
                path = game / relative
                if replacement and relative.lower() == 'data/lcs/world.ide':
                    path = replacement
                result.extend(definitions(path))
    return result


def alias(name, occupied, limit):
    candidate = 'lcs_' + name
    if len(candidate) > limit:
        candidate = 'lcs_' + name[:limit - 11] + '_' + hashlib.sha256(name.encode()).hexdigest()[:6]
    if candidate.lower() in occupied:
        raise ValueError('Namespace alias conflicts: ' + candidate)
    occupied.add(candidate.lower())
    return candidate


def archive_names(path):
    with path.open('rb') as stream:
        signature, count = struct.unpack('<4sI', stream.read(8))
        if signature != b'VER2':
            raise ValueError('Expected original SA IMG v2')
        return [struct.unpack('<IHH24s', stream.read(32))[3].split(b'\0')[0].decode('ascii')
                for _ in range(count)]


def prepare(game, source, output, bridge):
    root = pathlib.Path(__file__).resolve().parents[1]
    if output.exists():
        raise ValueError('Output exists; choose a new staging directory')
    manifest = json.loads((source / 'manifest.json').read_text())
    layout = json.loads((source / 'layout-validation.json').read_text())
    materials = json.loads((source / 'material-validation.json').read_text())
    geometry = json.loads((source / 'geometry-validation.json').read_text())
    if not layout['valid'] or not materials['complete'] or not geometry['valid']:
        raise ValueError('Source map validation failed')
    natives = profile_definitions(game, [game / 'data/default.dat'])
    for rel in ['data/maps/generic/' + name + '.ide' for name in GENERIC] + ['data/maps/veh_mods/veh_mods.ide', 'data/txdcut.ide']:
        natives.extend(definitions(game / rel))
    native_names = {fields[1].lower() for section, fields in natives}
    native_txds = {fields[2].lower() for section, fields in natives}
    original_definitions = profile_definitions(game, [game / 'data/default.dat', game / 'data/gta.dat'])
    original_names = {fields[1].lower() for section, fields in original_definitions}
    # Special models absent from IDEs survive core filtering. Their filenames,
    # and all retained stock dictionaries, also reserve streaming names.
    for filename in sum([archive_names(game / 'models' / name) for name in ('gta3.img', 'gta_int.img')], []):
        name, extension = filename.rsplit('.', 1)
        if extension.lower() == 'txd':
            native_txds.add(name.lower())
        elif extension.lower() == 'dff' and name.lower() not in original_names:
            native_names.add(name.lower())
    map_defs = list(definitions(source / 'layout-review/world.ide'))
    model_names = {fields[1].lower() for section, fields in map_defs}
    txds = {p.stem.lower() for p in (source / 'rw-assets').glob('*.txd')}
    names_occupied = native_names | model_names
    txds_occupied = native_txds | txds
    model_aliases = {fields[1].lower(): alias(fields[1], names_occupied, 19)
                     for section, fields in map_defs if fields[1].lower() in native_names or len(fields[1]) > 19}
    txd_aliases = {p.stem.lower(): alias(p.stem, txds_occupied, 19)
                   for p in sorted((source / 'rw-assets').glob('*.txd')) if p.stem.lower() in native_txds or len(p.stem) > 19}
    output.mkdir(parents=True)
    for rel in ['data/lcs', 'data/script', 'models/lcs', 'models/radar', 'metadata']:
        (output / rel).mkdir(parents=True)
    payload = output / 'metadata/packing-assets'
    payload.mkdir()
    for file in (source / 'rw-assets').iterdir():
        if file.suffix.lower() not in ('.dff', '.txd'):
            raise ValueError('Unexpected converted file: ' + file.name)
        mapping = model_aliases if file.suffix.lower() == '.dff' else txd_aliases
        name = mapping.get(file.stem.lower(), file.stem) + file.suffix.lower()
        if len(name.encode('ascii')) > 23:
            raise ValueError('Native IMG filename exceeds 23 bytes: ' + name)
        shutil.copyfile(file, payload / name)
    lines = []
    section = ''
    for line in (source / 'layout-review/world.ide').read_text().splitlines():
        if line in ('objs', 'tobj', 'txdp', 'end'):
            section = '' if line == 'end' else line
            lines.append(line)
            continue
        fields = [field.strip() for field in line.split(',')]
        if section == 'txdp':
            fields = [txd_aliases.get(field.lower(), field) for field in fields]
        elif section in ('objs', 'tobj'):
            physical_door = fields[1].lower() in PHYSICAL_DOOR_MODEL_NAMES
            fields[1] = model_aliases.get(fields[1].lower(), fields[1])
            fields[2] = txd_aliases.get(fields[2].lower(), fields[2])
            if section == 'objs' and physical_door:
                try:
                    fields[4] = str(int(fields[4], 0) | 0x800)
                except (ValueError, IndexError) as error:
                    raise ValueError('Garage-door IDE definition has invalid flags: ' + line) from error
        lines.append(', '.join(fields))
    (output / 'data/lcs/world.ide').write_text('\n'.join(lines) + '\n', encoding='ascii')
    ipl_files = sorted((source / 'layout-review').glob('world*.ipl'))
    for file in ipl_files:
        rows = []
        for line in file.read_text().splitlines():
            if ',' in line:
                fields = [field.strip() for field in line.split(',')]
                fields[1] = model_aliases.get(fields[1].lower(), fields[1])
                line = ', '.join(fields)
            rows.append(line)
        (output / 'data/lcs' / file.name).write_text('\n'.join(rows) + '\n', encoding='ascii')
    # Rename only COL header names; every geometric/surface byte stays intact.
    col = bytearray((source / 'world.col').read_bytes())
    offset = 0
    collision_names = set()
    extra_col_aliases = {}
    while offset < len(col):
        if col[offset:offset + 4] != b'COL2':
            raise ValueError('Expected native COL2')
        size = struct.unpack_from('<I', col, offset + 4)[0] + 8
        if size < 40 or offset + size > len(col):
            raise ValueError('Invalid COL2 record bounds')
        name = bytes(col[offset + 8:offset + 30]).split(b'\0')[0].decode('ascii')
        replacement = model_aliases.get(name.lower())
        if not replacement and name.lower() in native_names:
            replacement = alias(name, names_occupied, 19)
            extra_col_aliases[name] = replacement
        replacement = replacement or name
        col[offset + 8:offset + 30] = replacement.encode('ascii').ljust(22, b'\0')
        collision_names.add(replacement.lower())
        offset += size
    (output / 'models/lcs/world.col').write_bytes(col)
    asset_rows = list(manifest['models'].items()) + [(str(row.get('sourceModel', -1)), row) for row in manifest.get('renderLayers', [])]
    source_ids = {row['name'].lower(): row.get('sourceModel', int(key)) for key, row in asset_rows}
    bindings = []
    for section, fields in map_defs:
        original = fields[1]
        runtime = model_aliases.get(original.lower(), original)
        if runtime.lower() not in collision_names:
            raise ValueError('Missing collision for ' + runtime)
        bindings.append(f'{source_ids[original.lower()]}\t{fields[0]}\t{original}\t{runtime}')
    (output / 'data/lcs/model-bindings.tsv').write_text('\n'.join(bindings) + '\n', encoding='ascii')
    # Exact, instance-level candidates for the ASI's native garage binding.
    # Names alone are never sufficient at runtime: each row also carries the
    # converted IPL and XYZ placement, and the consumer must select a unique
    # entry in the corresponding native garage search window.
    door_models = {fields[1].lower(): fields for section, fields in map_defs
                   if section == 'objs' and fields[1].lower() in PHYSICAL_DOOR_MODEL_NAMES}
    door_bindings = []
    for file in ipl_files:
        section = ''
        instance_index = 0
        for raw in file.read_text(encoding='ascii').splitlines():
            line = raw.strip()
            if not line:
                continue
            if ',' not in line:
                section = line.lower()
                continue
            if section != 'inst':
                continue
            fields = [field.strip() for field in line.split(',')]
            current_index = instance_index
            instance_index += 1
            if len(fields) < 6:
                raise ValueError(f'Invalid IPL instance in {file.name}: {line}')
            original = fields[1]
            definition = door_models.get(original.lower())
            if not definition:
                continue
            # model id, source model id, name, IPL identity, and the original
            # converted coordinates form a stable binding across model aliases.
            runtime = model_aliases.get(original.lower(), original)
            door_bindings.append('\t'.join((
                str(source_ids[original.lower()]), definition[0], original, runtime,
                file.name, str(current_index), fields[3], fields[4], fields[5], 'object')))
    (output / 'data/lcs/door-bindings.tsv').write_text('\n'.join(door_bindings) + '\n', encoding='ascii')
    shutil.copyfile(source / 'water.dat', output / 'data/water.dat')
    for file in (source / 'radar').glob('radar*.txd'):
        shutil.copyfile(file, output / 'models/radar' / file.name)
    shutil.copyfile(root / 'out/world-stage/models/lcs/paths.img', output / 'models/lcs/paths.img')
    profile = ['# Stories Bridge map preview: sole Modloader gta.dat override',
               'IMG DATA\\PATHS\\CARREC.IMG', 'IMG DATA\\SCRIPT\\SCRIPT.IMG', 'IMG MODELS\\CUTSCENE.IMG',
               'IMG MODELS\\LCS\\PATHS.IMG', 'IMG MODELS\\LCS\\WORLD.IMG']
    profile.extend('IDE DATA\\MAPS\\generic\\' + name + '.IDE' for name in GENERIC)
    profile.extend(['IDE DATA\\MAPS\\veh_mods\\veh_mods.IDE', 'IDE DATA\\TXDCUT.IDE',
                    'IDE DATA\\LCS\\WORLD.IDE', 'COLFILE 0 MODELS\\LCS\\WORLD.COL',
                    'IPL DATA\\MAP.ZON', 'IPL DATA\\INFO.ZON'])
    profile.extend('IPL DATA\\LCS\\' + file.name for file in sorted((output / 'data/lcs').glob('world*.ipl')))
    (output / 'data/gta.dat').write_text('\n'.join(profile) + '\n', encoding='ascii')
    native_collisions = mount_native_collisions(output, game, natives)
    original = profile_definitions(game, [game / 'data/default.dat', game / 'data/gta.dat'])
    loaded = profile_definitions(game, [game / 'data/default.dat', output / 'data/gta.dat'], output / 'data/lcs/world.ide')
    # LCS can reuse a removed SA map name (helipad, sawmill). Such a name is
    # loaded by the new IDE, but must never retain the old SA mesh in core IMG.
    retained_native_names = {fields[1].lower() for section, fields in natives}
    excluded = sorted({fields[1] for section, fields in original if fields[1].lower() not in retained_native_names})
    excluded_path = output / 'metadata/excluded-sa-map-models.txt'
    excluded_path.write_text('\n'.join(excluded) + '\n', encoding='ascii')
    img = root / 'build/Release/lcs-img.exe'
    subprocess.run([str(img), 'pack', str(payload), str(output / 'models/lcs/world.img')], check=True)
    # Do not leave the packing directory in Modloader: duplicate assets would be loaded twice.
    for file in payload.iterdir():
        file.unlink()
    payload.rmdir()
    for name in ('gta3.img', 'gta_int.img'):
        subprocess.run([str(img), 'filter-native', str(game / 'models' / name), str(output / 'models' / name), str(excluded_path)], check=True)
    shutil.copyfile(root / 'out/world-stage/data/script/main.scm', output / 'data/script/main.scm')
    # Match the review snapshot's noon selection without touching the validated bootstrap control flow.
    code = bytearray((output / 'data/script/main.scm').read_bytes())
    if code[639:646] != b'\xc0\x00\x01\x09\x00\x00\x00':
        raise ValueError('Bootstrap clock operand changed; validate the new template')
    struct.pack_into('<i', code, 642, 12)
    (output / 'data/script/main.scm').write_bytes(export_player_globals(code))
    navigation = mount_navigation(output, source / 'paths') if (source / 'paths/paths.img').exists() else None
    shutil.copyfile(bridge, output / 'LcsSaBridge.asi')
    world_directory = game / 'modloader/stories_bridge/data/lcs'
    for name in ('render-ranges.tsv','island-zones.tsv'):
        shutil.copyfile(source / 'layout-review' / name, output / 'data/lcs' / name)
    (output / 'LcsSaBridge.ini').write_text('[LCS]\nMode=WorldPreview\nOriginalMapRenderRules=1\nWorldDirectory=' + str(world_directory) + '\n', encoding='utf-16')
    shutil.copyfile(source / 'lighting-source.json',output / 'metadata/lighting-source.json')
    report = dict(version=15 if navigation else 14, navigation=navigation, nativeObjectCollisions=len(native_collisions['records']), mode='WorldPreview', campaignEnabled=False, gameExecuted=False,
                  originalIslandLodSelection=True,originalLodMinimumRanges=True,sourceBasedNativeDayNightLighting=True,mapBackfaceCulling=False,
                  artistAtomicLodsSeparated=True,originalPhysicalCollisionSectors=True,
                  atomicCallbackCullNone=True,roadPassCullNone=True,renderStateRestoredAfterMapDraw=True,
                  sourceCoverageComplete=False, runtimeSceneStatesImplemented=False,
                  definitions=len(map_defs), collisions=len(collision_names), radarTiles=144,
                  sourceEntitiesMissingDff=71, pendingLodPairs=282,
                  modelAliases=model_aliases, textureDictionaryAliases=txd_aliases,
                  unregisteredCollisionAliases=extra_col_aliases,
                  internalTextureNamesChanged=False, sourceMap=str(source), gameDirectory=str(game),
                  nativeExecutableSHA256=hashlib.sha256((game / 'gta_sa.exe').read_bytes()).hexdigest(),
                  originalArchiveHashes={name: hashlib.file_digest((game / 'models' / name).open('rb'), 'sha256').hexdigest()
                                        for name in ('gta3.img', 'gta_int.img')},
                  modelStores=dict(collections.Counter(section for section, fields in loaded)))
    (output / 'metadata/map-preview.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    (output / 'LEIA.txt').write_text(
        ('Stories Bridge v15 - mapa e paths PS2\n\n' if navigation else 'Stories Bridge v14 - teste do mapa PS2\n\n') +
        'Abra uma NOVA PARTIDA para testar o mapa. A campanha nao esta ativada neste perfil.\n'
        'Inclui modelos, texturas internas originais, colisao COL2, radar e agua.\n'
        'Adapta a iluminacao PS2 a cores nativas SA de dia/noite; horas/clima intermediarios sao aproximados.\n'
        'Separa os atomics L0/L1 dos trilhos e demais modelos verificados.\n'
        'Mantem as pecas fisicas nos setores de colisao, inclusive com distancia LOD alta.\n'
        'ASI aplica CULLNONE no desenho dos atomics deste mapa, inclusive na passagem road.\n'
        'O estado de culling anterior e restaurado apos cada desenho. Teste visual ainda necessario.\n'
        'Conflitos entre nomes SA/LCS recebem aliases lcs_ documentados no manifesto.\n'
        'Pendente: 71 modelos colocados sem DFF, 282 pares LOD e estados dinamicos do cenario.\n'
        'Este pacote privado contem midia gerada a partir dos seus jogos.\n'
        'Distribua apenas o conversor sem assets, nunca esta pasta de teste.\n'
        'Nenhum jogo foi executado pelo instalador.\n', encoding='utf-8')
    print(json.dumps({key: report[key] for key in ('version', 'mode', 'definitions', 'collisions', 'modelStores')}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--source', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--bridge', type=pathlib.Path, required=True)
    args = parser.parse_args()
    prepare(args.game.resolve(), args.source.resolve(), args.output.resolve(), args.bridge.resolve())

"""Convert original PS2 gameplay Toni outfits using the user's native SA rig.

No media is embedded. The PS2 campaign uses plr/plr2..plr16, not the
legacy player/player2 variants retained in the console archive.
"""
import hashlib
import json
import pathlib
import struct
import subprocess
from ps2_world import Game, Chunk

LABELS = ('plr',) + tuple('plr' + str(i) for i in range(2, 17))


def run(exe, *args):
    result = subprocess.run([str(exe), *(str(x) for x in args)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + '\n' + result.stderr)
    return result.stdout


def extract_native_ped(archive, output, name='male01'):
    with archive.open('rb') as stream:
        signature, count = struct.unpack('<4sI', stream.read(8))
        if signature != b'VER2' or count > 100000:
            raise ValueError('Expected native SA IMG v2')
        rows = [struct.unpack('<IHH24s', stream.read(32)) for _ in range(count)]
        selected = [r for r in rows if r[3].split(b'\0')[0].lower() == (name.lower() + '.dff').encode('ascii')]
        if len(selected) != 1:
            raise ValueError('Native SA ' + name + '.dff must occur exactly once')
        sector, size, compressed, _ = selected[0]
        length = (compressed or size) * 2048
        if not length or sector * 2048 < 8 + count * 32 or sector * 2048 + length > archive.stat().st_size:
            raise ValueError('Invalid native reference entry')
        stream.seek(sector * 2048)
        output.write_bytes(stream.read(length))


def extra_models(game):
    # Original PS2 streaming suffix differs from the mobile layout. Only this
    # revision is accepted by the public frontend; validate directory bounds.
    c = game.chunk
    pointer = c.u32(game.streaming + 0x1e328)
    entries, capacity, count = c.read('<3I', pointer)
    if not 0 < count <= capacity <= 1000:
        raise ValueError('Invalid PS2 extra-model directory')
    result = {}
    for offset, sectors, rawname in c.read_array('<II24s', entries, count):
        name = rawname.split(b'\0')[0].decode('ascii').lower()
        if name in LABELS:
            if name in result or not sectors:
                raise ValueError('Duplicate/empty PS2 outfit')
            result[name] = (offset * 2048, sectors * 2048)
    if set(result) != set(LABELS):
        raise ValueError('Original 16 gameplay outfits incomplete')
    return result


def export_player(rom, sa_game, output, root):
    if output.exists():
        raise ValueError('Player output exists')
    output.mkdir(parents=True)
    intermediate = output / 'intermediate'
    intermediate.mkdir()
    assets = output / 'rw-assets'
    assets.mkdir()
    g = Game(rom, root / 'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    models = extra_models(g)
    game_file = intermediate / 'game.dat'
    game_file.write_bytes(g.chunk.data)
    reference = intermediate / 'native-male01.dff'
    extract_native_ped(sa_game / 'models/gta3.img', reference)
    rsl = root / 'build/Release/lcs-ps2-rsl.exe'
    rw = root / 'build/Release/lcs-convert-rw.exe'
    rows = []
    with (rom / 'MODELS/GTA3PS2.IMG').open('rb') as stream:
        for label in LABELS:
            pos, size = models[label]
            stream.seek(pos)
            data = stream.read(size)
            if len(data) != size or len(data) < 32:
                raise ValueError('Truncated PS2 gameplay model')
            length = struct.unpack_from('<I', data, 8)[0]
            if not 32 <= length <= size:
                raise ValueError('PS2 gameplay chunk exceeds stream')
            chunk = Chunk(data[:length], label)
            if chunk.u32(0) != 0x6d646c:
                raise ValueError('Expected PS2 MDL outfit')
            mdl = intermediate / (label + '.mdl')
            mdl.write_bytes(chunk.data)
            raw = intermediate / (label + '.dff')
            run(rsl, '-g', game_file, mdl, raw)
            asset = 'lcs_' + label
            conversion = run(rw, raw, assets / (asset + '.dff'), '--sa-ped', reference)
            rows.append(dict(label=label, asset=asset, originalOffset=pos, originalSectors=size//2048,
                             originalModelSHA256=hashlib.sha256(chunk.data).hexdigest(), conversion=conversion.strip()))
    # Decode named PS2 raster bytes directly, preserving internal names and
    # alpha. This is independent of the RSL model decoder's material linkage.
    dictionaries = {label: {} for label in LABELS}
    for texture in g.named_textures():
        dictionary = texture['dictionary'].lower()
        if dictionary in dictionaries:
            dictionaries[dictionary][texture['name']] = texture['image']
    tx_inputs = intermediate / 'textures'
    tx_inputs.mkdir()
    for label, images in dictionaries.items():
        if not images:
            raise ValueError('Original outfit dictionary empty: ' + label)
        with (tx_inputs / ('lcs_' + label + '.pst')).open('wb') as stream:
            stream.write(struct.pack('<4sI', b'PST1', len(images)))
            for name, (w, h, pixels) in sorted(images.items()):
                encoded = name.encode('ascii')
                if not 0 < len(encoded) <= 31:
                    raise ValueError('Invalid original internal texture name')
                stream.write(encoded.ljust(32, b'\0'))
                stream.write(struct.pack('<2I', w, h))
                stream.write(pixels)
    tx_output = intermediate / 'native-textures'
    run(root / 'build/Release/lcs-ps2-write.exe', tx_inputs, tx_output)
    for row in rows:
        asset = row['asset']
        (assets / (asset + '.txd')).write_bytes((tx_output / (asset + '.txd')).read_bytes())
        info = run(rw, assets / (asset + '.dff'), '--info')
        required = {line.removeprefix('texture=') for line in info.splitlines() if line.startswith('texture=')}
        available = set(dictionaries[row['label']])
        if not required or not required <= available or 'bones=32' not in info:
            raise ValueError('Missing texture or native skeleton: ' + row['label'])
        row.update(textures=sorted(required), sourceTextureNames=sorted(available), nativeBones=32,
                   dffSHA256=hashlib.sha256((assets / (asset + '.dff')).read_bytes()).hexdigest(),
                   txdSHA256=hashlib.sha256((assets / (asset + '.txd')).read_bytes()).hexdigest())
    validation = run(rw, assets, '--validate-actors')
    # Independent final-file audit: original topology, UV/weights and decoded
    # PS2 pixels, original bone lengths and native animation axes/helpers.
    import importlib.util
    audit_spec = importlib.util.spec_from_file_location('player_audit', root / 'tools/validate-ps2-player.py')
    audit_module = importlib.util.module_from_spec(audit_spec)
    audit_spec.loader.exec_module(audit_module)
    audit = audit_module.validate(rom, output, root)
    (output / 'index.tsv').write_text(''.join(row['label']+'\t'+row['asset']+'\n' for row in rows), encoding='ascii')
    report = dict(format=1, outfits=rows, initialOutfit='plr3', firstMissionOutfit='plr',
                  slots=[297,298], reference='SA male01 axes/hierarchy/helpers; original LCS bone lengths/mesh/UV/textures/weights',
                  referenceSHA256=hashlib.sha256(reference.read_bytes()).hexdigest(),
                  validation=validation.strip(), independentValidation=audit,
                  playerImgModified=False, gameExecuted=False)
    (output / 'manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report

"""Attach original per-model effects to the user's converted map archive.

Only Geometry effect plugins change. Geometry, textures, placements and archive
names are preserved. All original data and generated reports stay private.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from ps2_world import Game
from ps2_effects import effects_by_model, inject_dff


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def entries(stream):
    header = stream.read(8)
    if len(header) != 8 or header[:4] != b'VER2':
        raise ValueError('Expected map IMG v2')
    count, = struct.unpack_from('<I', header, 4)
    if not 0 < count < 100000:
        raise ValueError('Invalid map archive count')
    rows = []
    names = set()
    for _ in range(count):
        raw = stream.read(32)
        if len(raw) != 32:
            raise ValueError('Truncated map archive directory')
        sector, size, compressed, name = struct.unpack('<IHH24s', raw)
        if b'\0' not in name or compressed:
            raise ValueError('Unsupported map archive entry')
        name = name.split(b'\0')[0].decode('ascii')
        if not name or any(c in name for c in '/\\:') or name.lower() in names:
            raise ValueError('Unsafe or duplicate map archive name')
        names.add(name.lower())
        rows.append((sector, size, name))
    minimum = (8 + count*32 + 2047)//2048
    stream.seek(0, 2)
    length = stream.tell()
    intervals = sorted((s, s+n) for s, n, _ in rows)
    if any(not n or s < minimum or (s+n)*2048 > length for s, n, _ in rows) or any(a[1] > b[0] for a, b in zip(intervals, intervals[1:])):
        raise ValueError('Invalid map archive bounds')
    return rows


def dff_bytes(data):
    if len(data) < 12:
        raise ValueError('Truncated map DFF')
    length = struct.unpack_from('<I', data, 4)[0] + 12
    if length > len(data) or any(data[length:]):
        raise ValueError('Invalid DFF bounds/padding')
    return data[:length]


def rewrite_archive(path, effects):
    """Rewrite one private IMG atomically, preserving every untouched entry."""
    temporary = path.with_name(path.name+'.effects-new')
    if temporary.exists():
        raise ValueError('Unfinished effects archive already exists')
    changed = []
    with path.open('rb') as source:
        rows = entries(source)
        payloads = {}
        for sector, size, name in rows:
            if name.lower() not in effects:
                continue
            source.seek(sector*2048)
            original = dff_bytes(source.read(size*2048))
            converted = inject_dff(original, effects[name.lower()])
            payloads[name.lower()] = converted
            if converted != original:
                changed.append(dict(name=name, beforeSha256=hashlib.sha256(original).hexdigest(), afterSha256=hashlib.sha256(converted).hexdigest()))
        if set(payloads) != set(effects):
            raise ValueError('An effect model is absent from the map archive')
        if not changed:
            return []
        offset = (8+len(rows)*32+2047)//2048
        output_rows = []
        for sector, size, name in rows:
            data = payloads.get(name.lower())
            sectors = (len(data)+2047)//2048 if data is not None else size
            if not 0 < sectors <= 65535:
                raise ValueError('Effect DFF exceeds IMG entry capacity')
            output_rows.append((offset, sectors, name))
            offset += sectors
        with temporary.open('xb') as output:
            output.write(struct.pack('<4sI', b'VER2', len(rows)))
            for sector, size, name in output_rows:
                output.write(struct.pack('<IHH24s', sector, size, 0, name.encode('ascii')))
            for old, new in zip(rows, output_rows):
                output.write(bytes(new[0]*2048-output.tell()))
                data = payloads.get(old[2].lower())
                if data is None:
                    source.seek(old[0]*2048)
                    left = old[1]*2048
                    while left:
                        block = source.read(min(left, 1024*1024))
                        if not block:
                            raise ValueError('Truncated map archive payload')
                        output.write(block)
                        left -= len(block)
                else:
                    output.write(data)
                    output.write(bytes(new[1]*2048-len(data)))
    temporary.replace(path)  # never modify a hardlinked rollback inode
    return changed


def bound_effects(package, game):
    converted = effects_by_model(game)
    selected = {}
    bound = set()
    for raw in (package/'data/lcs/model-bindings.tsv').read_text(encoding='ascii').splitlines():
        source, runtime, original, alias = raw.split('\t')
        model = game.models.get(int(source))
        if model and original.lower() == (model.get('name') or '').lower() and converted.get(int(source)):
            key = alias.lower()+'.dff'
            if key in selected:
                raise ValueError('Ambiguous original effect model binding')
            selected[key] = converted[int(source)]
            bound.add(int(source))
    placed = {entity['model'] for entity in game.entities}
    if (set(converted)-bound) & placed:
        raise ValueError('A placed original light model has no principal runtime binding')
    return selected


def runtime_light_bindings(package, effects):
    rows = []
    for raw in (package/'data/lcs/model-bindings.tsv').read_text(encoding='ascii').splitlines():
        source, runtime, original, alias = raw.split('\t')
        modes = {row['source']['mode'] for row in effects.get(alias.lower()+'.dff', ())}
        if modes & {13, 14}:
            if not modes <= {13, 14}:
                raise ValueError('Mixed bridge and ordinary light effects require a per-effect gate')
            rows.append(f'{int(runtime)}\tbridge')
    return ('\n'.join(sorted(rows))+'\n' if rows else '').encode('ascii')


def mount(package, rom, names):
    game = Game(rom, names)
    effects = bound_effects(package, game)
    source_effects = effects_by_model(game)
    changes = rewrite_archive(package/'models/lcs/world.img', effects)
    bindings_path = package/'data/lcs/map-light-bindings.tsv'
    temporary = bindings_path.with_suffix('.tsv.new')
    temporary.write_bytes(runtime_light_bindings(package, effects))
    temporary.replace(bindings_path)
    report = dict(format=1, rom=str(rom.resolve()), names=str(names.resolve()),
                  sourceSha256=digest(rom/'CHK/PS2/GAME.DTZ'), namesSha256=digest(names),
                  bindingsSha256=digest(package/'data/lcs/model-bindings.tsv'),
                  lightBindingsSha256=digest(bindings_path),
                  archiveSha256=digest(package/'models/lcs/world.img'),
                  models=len(effects), lights=sum(len(v) for v in effects.values()),
                  sourceModelsWithLights=len(source_effects),
                  sourceLights=sum(len(v) for v in source_effects.values()),
                  unplacedSourceDefinitions=[dict(modelId=i, name=game.models[i]['name'], lights=len(rows))
                      for i, rows in source_effects.items()
                      if not any(entity['model'] == i for entity in game.entities)],
                  changed=changes, gameExecuted=False, visualVerified=False)
    target = package/'metadata/map-effects.json'
    target.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


def expected_effects(package):
    """Re-read the original input; manifest counts never supply effect data."""
    target = package/'metadata/map-effects.json'
    if not target.exists():
        return {}
    report = json.loads(target.read_text(encoding='utf-8'))
    campaign = json.loads((package/'metadata/campaign.json').read_text())
    pin = campaign.get('mapEffects', {})
    if pin.get('manifest') != 'metadata/map-effects.json' or pin.get('sha256') != digest(target):
        raise ValueError('Original effect manifest is not pinned by the campaign')
    rom, names = Path(report['rom']), Path(report['names'])
    if report.get('format') != 1 or report.get('gameExecuted') is not False or report.get('visualVerified') is not False:
        raise ValueError('Invalid original effect scope')
    if digest(rom/'CHK/PS2/GAME.DTZ') != report['sourceSha256'] or digest(names) != report['namesSha256']:
        raise ValueError('Original effect input changed')
    if digest(package/'data/lcs/model-bindings.tsv') != report['bindingsSha256'] or digest(package/'models/lcs/world.img') != report['archiveSha256']:
        raise ValueError('Effect model bindings/archive changed')
    effects = bound_effects(package, Game(rom, names))
    light_bindings = package/'data/lcs/map-light-bindings.tsv'
    if digest(light_bindings) != report['lightBindingsSha256'] or light_bindings.read_bytes() != runtime_light_bindings(package, effects):
        raise ValueError('Owned bridge light bindings changed')
    if report['models'] != len(effects) or report['lights'] != sum(len(v) for v in effects.values()):
        raise ValueError('Original effect coverage changed')
    return effects


def native_reread(package, working, converter):
    """Independently decode the changed Geometry plugins with librwgta."""
    effects = expected_effects(package)
    if working.exists():
        raise ValueError('Choose a new native reread working directory')
    working.mkdir(parents=True)
    with (package/'models/lcs/world.img').open('rb') as stream:
        for sector, size, name in entries(stream):
            if name.lower() in effects:
                stream.seek(sector*2048)
                (working/name).write_bytes(dff_bytes(stream.read(size*2048)))
    result = subprocess.run([str(converter.resolve()), str(working.resolve()), '--map-effects-inventory'],
                            capture_output=True, text=True, check=True)
    actual = {}
    for line in result.stdout.splitlines():
        fields = line.split('\t')
        if len(fields) != 8 or fields[0] != 'F':
            raise ValueError('Unexpected native effect inventory record')
        key = (fields[1].lower()+'.dff', int(fields[2]))
        if key in actual:
            raise ValueError('Duplicated native effect')
        actual[key] = tuple(map(int, fields[3:]))
    expected = {(name, i):(row['sa']['flags'], row['sa']['flashiness'], *row['color'][:3])
                for name, rows in effects.items() for i, row in enumerate(rows)}
    if actual != expected:
        raise ValueError('Native Geometry effect decoding differs from the original conversion')
    return dict(valid=True, models=len(effects), lights=len(expected),
                nativeDecoderSha256=digest(converter), gameExecuted=False)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('package', 'working', 'converter'):
        parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(native_reread(**vars(args)), indent=2))

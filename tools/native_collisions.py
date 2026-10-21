"""Restore native SA object collisions from the user's own original archives.

Filtering SA world IPLs/COLs must not remove the collision dependency of a
retained pickup/prop. Explicitly load only records named by retained native
IDEs, without any original SA map placement or additional IMG slot.
"""
import hashlib
import json
import pathlib
import struct


RESOURCE = 'models/lcs/native-objects.col'
DIRECTIVE = 'COLFILE 0 MODELS\\LCS\\NATIVE-OBJECTS.COL'


def retained_definitions(game):
    files = []
    for line in (game / 'data/default.dat').read_text(encoding='ascii').splitlines():
        text = line.split('#')[0].strip()
        if text.upper().startswith('IDE '):
            files.append(game / text[4:].strip().replace('\\', '/'))
    files.extend(game / ('data/maps/generic/' + name + '.ide')
                 for name in ('vegepart', 'barriers', 'dynamic', 'dynamic2', 'multiobj', 'procobj'))
    files.extend(game / name for name in ('data/maps/veh_mods/veh_mods.ide', 'data/txdcut.ide'))
    result = []
    for file in files:
        section = ''
        for line in file.read_text(encoding='ascii').splitlines():
            text = line.split('#')[0].strip()
            if not text:
                continue
            if text == 'end':
                section = ''
            elif ',' not in text:
                section = text
            elif section in ('objs', 'tobj', 'weap', 'hier', 'anim', 'cars', 'peds'):
                result.append((section, [f.strip() for f in text.split(',')]))
    return result


def records(data, padded=False):
    offset = 0
    while offset < len(data):
        if padded and not any(data[offset:]):
            return
        if len(data) - offset < 32:
            raise ValueError('Truncated native COL header')
        signature = data[offset:offset + 4]
        length = struct.unpack_from('<I', data, offset + 4)[0] + 8
        minimum = 72 if signature == b'COLL' else 104
        if signature not in (b'COLL', b'COL2', b'COL3', b'COL4') or length < minimum or offset + length > len(data):
            raise ValueError('Invalid native COL record bounds/version')
        raw_name = data[offset + 8:offset + 30]
        if b'\0' not in raw_name:
            raise ValueError('Unterminated native COL name')
        name = raw_name.split(b'\0')[0].decode('ascii')
        if not name or any(c in name for c in '/\\:'):
            raise ValueError('Invalid native COL name')
        yield name.lower(), data[offset:offset + length]
        offset += length


def archive_collisions(path):
    with path.open('rb') as stream:
        header = stream.read(8)
        if len(header) != 8 or header[:4] != b'VER2':
            raise ValueError('Expected native IMG v2')
        count = struct.unpack_from('<I', header, 4)[0]
        minimum = ((8 + 32 * count + 2047) // 2048) * 2048
        if minimum > path.stat().st_size:
            raise ValueError('IMG directory out of bounds')
        entries = []
        for _ in range(count):
            entry = stream.read(32)
            if len(entry) != 32:
                raise ValueError('Truncated IMG directory')
            sector, size, compressed, raw_name = struct.unpack('<IHH24s', entry)
            if b'\0' not in raw_name:
                raise ValueError('Unterminated IMG filename')
            name = raw_name.split(b'\0')[0].decode('ascii')
            offset, length = sector * 2048, (compressed or size) * 2048
            if offset < minimum or not length or offset + length > path.stat().st_size:
                raise ValueError('IMG payload out of bounds')
            if name.lower().endswith('.col'):
                entries.append((name, offset, length))
        for name, offset, length in entries:
            stream.seek(offset)
            for model, record in records(stream.read(length), padded=True):
                yield model, record, path.name + '/' + name


def source_records(game, definitions):
    # Peds/vehicles use their native embedded/temp collision backend. These
    # records are for retained objects, not for the converted LCS map names.
    names = {f[1].lower(): int(f[0]) for section, f in definitions
             if section in ('objs', 'tobj', 'anim', 'hier', 'weap')}
    selected = {}
    for archive in ('gta3.img', 'gta_int.img'):
        for name, record, origin in archive_collisions(game / 'models' / archive):
            if name not in names:
                continue
            if name in selected and selected[name][0] != record:
                raise ValueError('Ambiguous source collision for ' + name)
            selected.setdefault(name, (record, origin, names[name]))
    if names.get('money') != 1212 or 'money' not in selected:
        raise ValueError('Native Money 1212 collision dependency missing')
    return selected


def mount(package, game, definitions):
    selected = source_records(game, definitions)
    payload = b''.join(selected[name][0] for name in sorted(selected))
    (package / RESOURCE).write_bytes(payload)
    profile = package / 'data/gta.dat'
    lines = [line for line in profile.read_text(encoding='ascii').splitlines()
             if line.upper() != DIRECTIVE]
    # Native IDEs must exist before explicit collision loading.
    position = next(i for i, line in enumerate(lines) if line.startswith('COLFILE '))
    lines.insert(position, DIRECTIVE)
    profile.write_text('\n'.join(lines) + '\n', encoding='ascii')
    report = dict(format=1, resource=RESOURCE, explicitLoad=True,
                  sourceGame=str(game), gameExecuted=False,
                  records=[dict(name=name, modelId=selected[name][2], source=selected[name][1],
                                bytes=len(selected[name][0]), sha256=hashlib.sha256(selected[name][0]).hexdigest())
                           for name in sorted(selected)],
                  payloadSHA256=hashlib.sha256(payload).hexdigest())
    (package / 'metadata/native-collisions.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


def validate(package, game, definitions):
    selected = source_records(game, definitions)
    path = package / RESOURCE
    if not path.is_file():
        raise ValueError('Retained native object collision pack is missing (including Money 1212)')
    actual = list(records(path.read_bytes()))
    if len(actual) != len(selected) or len({name for name, _ in actual}) != len(actual):
        raise ValueError('Native object collision inventory mismatch')
    for name, record in actual:
        if name not in selected or record != selected[name][0]:
            raise ValueError('Native object collision differs from original SA: ' + name)
    directives = (package / 'data/gta.dat').read_text(encoding='ascii').upper().splitlines()
    if directives.count(DIRECTIVE) != 1:
        raise ValueError('Native collision pack is not loaded exactly once')
    if directives.index(DIRECTIVE) > next(i for i, s in enumerate(directives) if s.startswith('IPL ')):
        raise ValueError('Native collisions must be loaded before placements')
    metadata = json.loads((package / 'metadata/native-collisions.json').read_text())
    expected_rows = [dict(name=name, modelId=selected[name][2], source=selected[name][1],
                          bytes=len(selected[name][0]), sha256=hashlib.sha256(selected[name][0]).hexdigest())
                     for name in sorted(selected)]
    if metadata['records'] != expected_rows or metadata['payloadSHA256'] != hashlib.sha256(path.read_bytes()).hexdigest():
        raise ValueError('Native collision provenance mismatch')
    return dict(valid=True, records=len(actual), originalCollisionBytesPreserved=True,
                moneyModelId=1212, moneySource=selected['money'][1], explicitLoad=True)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    parser.add_argument('--package', type=pathlib.Path, required=True)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    game, package = args.game.resolve(), args.package.resolve()
    if args.verify:
        result = validate(package, game, retained_definitions(game))
    else:
        result = mount(package, game, retained_definitions(game))
        result = dict(records=len(result['records']), resource=result['resource'], gameExecuted=False)
    print(json.dumps(result, indent=2))

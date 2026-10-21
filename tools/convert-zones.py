"""Convert the user's LCS DATA/GTA3.ZON into native SA INFO.ZON/MAP.ZON.

Source format (III/LCS CFileLoader::LoadZone): name, type, minx, miny, minz,
maxx, maxy, maxz, level. Types: 0 default (area names), 1 navigation, 2 info
(population bounds duplicating the names), 3 map zones (island streaming).
Destination: SA IPL "zone" rows: name, type, min xyz, max xyz, level, label.

- INFO.ZON: LCS name zones (types 0/1) become SA navigation zones (type 0); the
  label is the original LCS GXT key, so SA's own zone-name consumer shows the
  original text from the converted GXT. VICE_C (the retail default root also
  used by the police-report zone table; GXT "Liberty City") is the default
  covering zone, with the source CTheZones::Init bounds.
- MAP.ZON: SA map zones only select the city level (police/gang models); they
  do not stream geometry. LCS uses one police force everywhere and the package
  mounts it in the LOS_SANTOS (level 1) police slots, so one covering map zone
  at level 1 replaces the 103 LCS streaming map zones (SA holds at most 40).
- Type 2 info zones are not copied: SA attaches zone info to navigation zones,
  and every LCS info zone duplicates the bounds of a name zone.
No game data is bundled; the tool reads the user's own GTA3.ZON.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import zlib

# CTheZones::Init (source): default zones cover -2000..2000 xy, -500..500 z.
WORLD = (-2000.0, -2000.0, -500.0, 2000.0, 2000.0, 500.0)
SA_NAVIGATION_LIMIT = 380   # CTheZones::NavigationZoneArray, minus its default entry
SA_LEVEL_LOS_SANTOS = 1


def parse_source(text):
    rows, section = [], None
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if section is None:
            if line.lower() != 'zone':
                raise ValueError('GTA3.ZON must start with a zone section')
            section = 'zone'
            continue
        if line.lower() == 'end':
            section = 'end'
            continue
        if section != 'zone':
            raise ValueError('Data after the zone section end')
        fields = [f.strip() for f in line.split(',') if f.strip()]
        if len(fields) != 9:
            raise ValueError(f'GTA3.ZON line {number}: expected 9 fields')
        name, kind = fields[0], int(fields[1])
        values = [float(v) for v in fields[2:8]]
        level = int(fields[8])
        if not 1 <= len(name) <= 7 or not name.isascii() or not name.replace('_', '').isalnum():
            raise ValueError(f'GTA3.ZON line {number}: invalid zone name')
        if kind not in (0, 1, 2, 3) or not 0 <= level <= 4:
            raise ValueError(f'GTA3.ZON line {number}: invalid zone type/level')
        # CTheZones::CreateZone swaps inverted min/max per axis (retail Box28 has an inverted z).
        bounds = tuple(min(values[i], values[i + 3]) for i in range(3)) + \
                 tuple(max(values[i], values[i + 3]) for i in range(3))
        rows.append(dict(name=name, type=kind, bounds=bounds, level=level))
    if section != 'end' or not rows:
        raise ValueError('GTA3.ZON zone section is incomplete')
    return rows


def gxt_keys(path):
    """JAMCRC keys of the MAIN table of a converted SA (8-bit) GXT."""
    data = pathlib.Path(path).read_bytes()
    if len(data) < 12 or data[4:8] != b'TABL':
        raise ValueError('Converted GXT is not an SA TABL file')
    size = struct.unpack_from('<I', data, 8)[0]
    main = struct.unpack_from('<I', data, 12 + 8)[0]
    if size < 12 or data[main:main + 4] != b'TKEY':
        raise ValueError('Converted GXT MAIN table is missing')
    count = struct.unpack_from('<I', data, main + 4)[0] // 8
    return {struct.unpack_from('<I', data, main + 8 + 8 * i + 4)[0] for i in range(count)}


def jamcrc(key):
    return ~zlib.crc32(key.encode('ascii')) & 0xFFFFFFFF


def fmt(value):
    return format(value, '.6g') if value != int(value) else f'{value:.1f}'


def zone_line(name, kind, bounds, level, label):
    return ', '.join([name, str(kind), *(fmt(v) for v in bounds), str(level), label])


def convert(text, gxt=None):
    rows = parse_source(text)
    names = [r for r in rows if r['type'] in (0, 1)]
    if len({r['name'] for r in names}) != len(names):
        raise ValueError('Duplicate LCS name zone')
    if len(names) + 1 > SA_NAVIGATION_LIMIT:
        raise ValueError('LCS name zones exceed SA navigation capacity')
    labels = ['VICE_C'] + [r['name'] for r in names]
    if gxt is not None:
        keys = gxt_keys(gxt)
        missing = [k for k in labels if jamcrc(k) not in keys]
        if missing:
            raise ValueError('Converted GXT lacks zone text keys: ' + ', '.join(missing))
    info = ['zone', zone_line('VICE_C', 0, WORLD, SA_LEVEL_LOS_SANTOS, 'VICE_C')]
    info += [zone_line(r['name'], 0, r['bounds'], SA_LEVEL_LOS_SANTOS, r['name']) for r in names]
    info.append('end')
    mapzones = ['zone', zone_line('LIBERTY', 3, WORLD, SA_LEVEL_LOS_SANTOS, 'UNUSED'), 'end']
    report = dict(format=1, sourceZones=len(rows),
                  sourceTypes={str(t): sum(r['type'] == t for r in rows) for t in range(4)},
                  navigationZones=len(info) - 2, defaultNavigationZone='VICE_C',
                  mapZones=1, mapLevel='LOS_SANTOS', droppedInfoZones=sum(r['type'] == 2 for r in rows),
                  replacedStreamingMapZones=sum(r['type'] == 3 for r in rows),
                  gxtKeysVerified=gxt is not None, gameExecuted=False)
    return '\r\n'.join(info) + '\r\n', '\r\n'.join(mapzones) + '\r\n', report


def write(source, output, gxt=None):
    output = pathlib.Path(output)
    raw = pathlib.Path(source).read_bytes()
    info, mapzones, report = convert(raw.decode('ascii'), gxt)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'info.zon').write_bytes(info.encode('ascii'))
    (output / 'map.zon').write_bytes(mapzones.encode('ascii'))
    report.update(sourceSha256=hashlib.sha256(raw).hexdigest(),
                  infoSha256=hashlib.sha256(info.encode('ascii')).hexdigest(),
                  mapSha256=hashlib.sha256(mapzones.encode('ascii')).hexdigest())
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path, help='LCS DATA/GTA3.ZON from the user copy')
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--gxt', type=pathlib.Path, help='converted american.gxt to verify zone labels')
    args = parser.parse_args()
    print(json.dumps(write(args.source, args.output, args.gxt), indent=2))

"""Directed checks for tools/convert-zones.py with synthetic (authored) zones."""
import importlib.util
import pathlib
import struct
import sys
import tempfile
import zlib

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('convert_zones', root/'tools/convert-zones.py')
zones = importlib.util.module_from_spec(spec)
spec.loader.exec_module(zones)

SOURCE = '\r\n'.join([
    'zone',
    'AREA_A,\t0,\t10.5,\t-20,\t-5,\t30,\t40,\t50,\t1,\t',
    'BRIDGE,\t1,\t0,\t0,\t0,\t5,\t5,\t5,\t1,\t',
    'AREA_A,\t2,\t10.5,\t-20,\t-5,\t30,\t40,\t50,\t1,\t',
    'Box1,\t3,\t-10,\t-10,\t9,\t10,\t10,\t-9,\t2,\t',
    'end', ''])


def gxt(keys):
    entries = b''.join(struct.pack('<II', 0, ~zlib.crc32(k.encode()) & 0xFFFFFFFF) for k in keys)
    tkey = b'TKEY' + struct.pack('<I', len(entries)) + entries
    table = b'TABL' + struct.pack('<I', 12) + b'MAIN\0\0\0\0' + struct.pack('<I', 4 + 8 + 12)
    return struct.pack('<HH', 4, 8) + table + tkey + b'TDAT' + struct.pack('<I', 0)


def check(value, message):
    if not value:
        raise AssertionError(message)


rows = zones.parse_source(SOURCE)
check([r['type'] for r in rows] == [0, 1, 2, 3], 'All four source zone types parse')
check(rows[3]['bounds'] == (-10.0, -10.0, -9.0, 10.0, 10.0, 9.0), 'Inverted source axis is swapped like CTheZones::CreateZone')
info, mapzones, report = zones.convert(SOURCE)
lines = info.strip().split('\r\n')
check(lines[0] == 'zone' and lines[-1] == 'end', 'INFO.ZON is one native zone section')
check(lines[1] == 'VICE_C, 0, -2000.0, -2000.0, -500.0, 2000.0, 2000.0, 500.0, 1, VICE_C', 'Retail default root covers source Init bounds')
check(lines[2] == 'AREA_A, 0, 10.5, -20.0, -5.0, 30.0, 40.0, 50.0, 1, AREA_A', 'Name zone keeps bounds and uses its GXT key')
check(lines[3].startswith('BRIDGE, 0,') and len(lines) == 5, 'Navigation type 1 is shown by SA; info type 2 duplicate is not copied')
check(mapzones.strip().split('\r\n')[1] == 'LIBERTY, 3, -2000.0, -2000.0, -500.0, 2000.0, 2000.0, 500.0, 1, UNUSED',
      'One LOS_SANTOS map zone replaces streaming map zones')
check(report['droppedInfoZones'] == 1 and report['replacedStreamingMapZones'] == 1 and not report['gxtKeysVerified'], 'Report counts')
with tempfile.TemporaryDirectory() as directory:
    good = pathlib.Path(directory)/'good.gxt'
    good.write_bytes(gxt(['VICE_C', 'AREA_A', 'BRIDGE']))
    check(zones.convert(SOURCE, good)[2]['gxtKeysVerified'], 'Converted GXT with every zone key is accepted')
    bad = pathlib.Path(directory)/'bad.gxt'
    bad.write_bytes(gxt(['VICE_C', 'AREA_A']))
    try:
        zones.convert(SOURCE, bad)
        raise AssertionError('Missing zone text key must be refused')
    except ValueError as error:
        check('BRIDGE' in str(error), 'Refusal names the missing key')
for broken in ('AREA_A,\t0,\t1,\t2\r\n', 'zone\r\nTOO_LONG_NAME,\t0,\t0,\t0,\t0,\t1,\t1,\t1,\t1,\t\r\nend', 'zone\r\nA,\t9,\t0,\t0,\t0,\t1,\t1,\t1,\t1,\t\r\nend'):
    try:
        zones.parse_source(broken)
        raise AssertionError('Malformed source must be refused')
    except ValueError:
        pass
if len(sys.argv) == 3:
    # Optional: the user's own GTA3.ZON and converted american.gxt.
    _, _, user = zones.convert(pathlib.Path(sys.argv[1]).read_bytes().decode('ascii'), pathlib.Path(sys.argv[2]))
    check(user['gxtKeysVerified'] and user['navigationZones'] > 1, 'User zones convert with verified text keys')
print('PASS: zone parsing/normalization, native INFO/MAP.ZON rows, GXT key verification and malformed-source refusal')

"""Pins of the native SA code behind the Import/Export garage consumers, as data only.

Usage: garage_door_pin_tests.py <gta_sa.exe> [LcsSaBridge.asi]

Every byte profile is parsed out of src/sa_garage_door.cpp and compared with the SA 1.0 US image, so a
source change cannot silently drift from what was verified. The optional ASI argument additionally proves
the compiled module carries the same profiles. The game is never started.
"""
import importlib.util
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    sys.modules[name] = loaded
    spec.loader.exec_module(loaded)
    return loaded


def profile(source, name):
    match = re.search(r'constexpr unsigned char %s\[\]=\{(.*?)\};' % name, source, re.S)
    assert match, name
    return bytes(int(token, 16) for token in re.findall(r'0x([0-9A-Fa-f]{2})', match[1]))


def main(executable, asi=None):
    native = module('garage_door_native_inspect', ROOT/'tools/inspect-native-command.py')
    image = native.NativeImage(pathlib.Path(executable))
    source = (ROOT/'src/sa_garage_door.cpp').read_text()
    init = profile(source, 'init_profile')
    stream = profile(source, 'stream_profile')
    stream_end = profile(source, 'stream_end_profile')
    height = profile(source, 'height_profile')

    # CGarages::Init zeroes CarTypesCollected[0..3]; Save and Load stream exactly four words from the same array.
    assert image.read(0x447156, len(init)) == init
    for address in (0x5D31C1, 0x5D32D2):
        assert image.read(address, len(stream)) == stream
    for address in (0x5D31D4, 0x5D32E5):
        assert image.read(address, len(stream_end)) == stream_end
    assert struct.pack('<I', 0x96BFFC) in stream_end           # loop bound: four 32-bit entries
    # SA's CGarages::FindDoorHeightForMI is the formula the consumer reads from the live collision model.
    assert image.read(0x449C30, len(height)) == height
    assert struct.unpack('<f', image.read(0x858B1C, 4))[0] == struct.unpack('<f', struct.pack('<f', 0.1))[0]

    # The array has no other code user: only Init, Save and Load form its addresses.
    users = []
    for start, size in image.executable_spans:
        base = 0x400000+start
        data = image.read(base, size)
        for slot in (0x96BFEC, 0x96BFF0, 0x96BFF4, 0x96BFF8):
            needle = struct.pack('<I', slot)
            position = data.find(needle)
            while position >= 0:
                users.append(base+position)
                position = data.find(needle, position+1)
    assert sorted(users) == [0x447158, 0x44715E, 0x447164, 0x44716A, 0x5D31C2, 0x5D32D3], [hex(a) for a in sorted(users)]
    # Collector geometry uses existing native garage queries, pinned at retail callsites.
    crusher_source = (ROOT/'src/sa_crusher.cpp').read_text()
    for name, address in (('touching_profile_a', 0x44B8B1), ('touching_profile_b', 0x44C642), ('outside_profile', 0x44B913)):
        blob = profile(crusher_source, name)
        assert image.read(address, len(blob)) == blob, name

    # Controlled object pool classes: the shared building pool root and the model-info table already pinned by the
    # bridge and crusher consumers are used, not redefined.
    assert '0xB74498' in source and '0xA9B0C8' in source

    if asi:
        compiled = pathlib.Path(asi).read_bytes()
        for name, blob in (('init', init), ('stream', stream), ('stream_end', stream_end), ('height', height)):
            assert compiled.count(blob) >= 1, f'{name} profile is not in the compiled module'
    print('PASS: import garage native pins match SA 1.0 US; saved list has only Init/Save/Load users')


if __name__ == '__main__':
    main(*sys.argv[1:3])

"""Pins of the native SA code behind the crusher crane consumer, as data only.

Usage: crane_pin_tests.py <gta_sa.exe> [LcsSaBridge.asi]

The byte profiles are parsed from src/sa_crane.cpp and compared with the SA 1.0 US image (CPlaceable::SetHeading,
CMatrix::SetRotateZOnly, and CEntity::SetIsStatic(false) for the drop wake; the Remove/UpdateRW/Add sequence and
vehicle weak-reference helpers are the already pinned bridge consumers). The game is never started.
"""
import importlib.util
import pathlib
import re
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
    native = module('crane_native_inspect', ROOT/'tools/inspect-native-command.py')
    image = native.NativeImage(pathlib.Path(executable))
    source = (ROOT/'src/sa_crane.cpp').read_text()
    set_heading = profile(source, 'set_heading_profile')
    rotate_z = profile(source, 'rotate_z_profile')
    set_is_static = profile(source, 'set_is_static_profile')
    assert image.read(0x43E0C0, len(set_heading)) == set_heading
    assert image.read(0x59B020, len(rotate_z)) == rotate_z
    assert image.read(0x403E20, len(set_is_static)) == set_is_static
    # SetHeading jumps to exactly the pinned SetRotateZOnly when the entity owns a matrix.
    assert image.read(0x43E0C9, 1) == b'\xE9'
    displacement = int.from_bytes(image.read(0x43E0CA, 4), 'little', signed=True)
    assert 0x43E0CE + displacement == 0x59B020
    for address in (0x534AE0, 0x446F90, 0x532B00, 0x533020, 0x43E0C0, 0x403E20):
        assert image.executable(address)
    for text in ('0xA9B0C8', '0xB74498', '0x534AE0', '0x420B80', '0x403E20', '0x43E0C0', '0x533020'):
        assert text in source
    if asi:
        compiled = pathlib.Path(asi).read_bytes()
        assert set_heading in compiled and rotate_z in compiled, 'compiled module lacks the crane profiles'
    print('PASS: crusher crane native pins match SA 1.0 US')


if __name__ == '__main__':
    main(*sys.argv[1:3])

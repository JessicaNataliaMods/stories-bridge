"""Pins of the SA engine facts src/sa_threat_occlusion.cpp relies on (1383), as data only.

Usage: threat_occlusion_pin_tests.py <gta_sa.exe>
(MSVC folds short byte profiles into immediate compares, so the built bridge is not byte-searched; the arrays are parsed
from the source and compared with the SA 1.0 US image.)

Proved here: the chain ScanForEvents -> acquaintance scan -> FindClosestAcquaintance -> CanPedTargetPed -> CanPedTargetPoint
-> CWorld::GetIsLineOfSightClear is the threat scan; CanPedTargetPoint keeps its ped argument in ESI from entry to the
single GetIsLineOfSightClear call at 0x5F1C31 (nothing writes ESI in between); that call is `E8 rel32 -> 0x56A490` with the
nine cdecl arguments pushed as (buildings=1, vehicles=0, peds=0, objects=1, dummies=0, seeThrough=1, camera=0), so `objects`
sits at [esp+0x18] on entry to a function called from that site; GetIsLineOfSightClear's own parameter order (objects is
the sixth) is checked against the reversed source. Limit: static decoding, nothing is executed.
"""
import pathlib
import re
import struct
import sys

import capstone

ROOT = pathlib.Path(__file__).resolve().parents[1]


def image_reader(path):
    data = pathlib.Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 52)[0]
    sections = []
    for i in range(count):
        o = pe + 24 + optional + i * 40
        size, va, raw_size, raw = struct.unpack_from('<IIII', data, o + 8)
        sections.append((base + va, max(size, raw_size), raw))

    def read(address, size):
        for va, length, raw in sections:
            if va <= address and address + size <= va + length:
                return data[raw + address - va:raw + address - va + size]
        raise AssertionError(hex(address))
    return read


def profile(source, name):
    match = re.search(r'constexpr uint8_t %s\[\d*\]\s*=\s*\{(.*?)\};' % name, source, re.S)
    assert match, name
    return bytes(int(token, 16) for token in re.findall(r'0x([0-9A-Fa-f]{2})', match[1]))


def call_target(read, address):
    assert read(address, 1) == b'\xE8', hex(address)
    return address + 5 + struct.unpack('<i', read(address + 1, 4))[0]


def main(executable):
    read = image_reader(executable)
    source = (ROOT / 'src/sa_threat_occlusion.cpp').read_text(encoding='utf-8')
    pins = {'scan_call': 0x607EA2, 'closest_call': 0x607E16, 'target_call': 0x607CAD,
            'point_call': 0x5F1C9A, 'sight': 0x5F1C06}
    for name, address in pins.items():
        expected = profile(source, name)
        assert read(address, len(expected)) == expected, name
    assert call_target(read, 0x607EA8) == 0x607D80
    assert call_target(read, 0x607E16) == 0x607A90
    assert call_target(read, 0x607CB0) == 0x5F1C40
    assert call_target(read, 0x5F1CA1) == 0x5F1B70
    assert call_target(read, 0x5F1C31) == 0x56A490
    assert re.search(r'call_site=0x5F1C31,los_function=0x56A490,get_ped_function=0x54FF90', source)
    assert profile(source, 'expected_call') == read(0x5F1C31, 5)

    # push order before GetIsLineOfSightClear (cdecl, the last push is the first argument)
    sight = profile(source, 'sight')
    pushes = [sight[i + 1] for i in range(len(sight) - 1) if sight[i] == 0x6A]
    assert pushes == [0, 1, 0, 1, 0, 0, 1], pushes          # camera, seeThrough, dummies, objects, peds, vehicles, buildings
    world = (ROOT / 'vendor/sa-reference/World.h').read_text(encoding='utf-8')
    signature = re.search(r'static bool GetIsLineOfSightClear\((.*?)\);', world)[1]
    names = [p.split('=')[0].strip().split()[-1] for p in signature.split(',')]
    assert names == ['origin', 'target', 'buildings', 'vehicles', 'peds', 'objects', 'dummies', 'doSeeThroughCheck', 'doCameraIgnoreCheck'], names
    # objects is the 6th argument: on entry to a callee reached through `call` it is at [esp + 4 + 4*5] = [esp+0x18]
    assert 4 + 4 * names.index('objects') == 0x18 and '[esp+0x18]' in source.replace(' ', '')

    # CanPedTargetPoint keeps the ped (its first argument) in ESI for the whole function
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    instructions = list(md.disasm(read(0x5F1B70, 0x5F1C36 - 0x5F1B70), 0x5F1B70))
    assert instructions[0].mnemonic == 'sub' and instructions[0].op_str == 'esp, 0xc'
    assert instructions[1].mnemonic == 'push' and instructions[1].op_str == 'esi'
    assert instructions[2].mnemonic == 'mov' and instructions[2].op_str == 'esi, dword ptr [esp + 0x14]', instructions[2].op_str
    for ins in instructions[3:]:
        if ins.address >= 0x5F1C31:
            break
        written = ins.op_str.split(',')[0].strip() if ins.mnemonic in ('mov', 'lea', 'add', 'sub', 'xor', 'or', 'and', 'inc', 'dec', 'movzx', 'movsx', 'xchg') else ''
        assert written != 'esi', 'ESI written at %#x: %s %s' % (ins.address, ins.mnemonic, ins.op_str)
    assert instructions[-1].address == 0x5F1C31 and instructions[-1].mnemonic == 'call'
    # Only the caller CanPedTargetPed reaches it (so ESI is a CPed*)
    caller = [i for i in md.disasm(read(0x5F1C40, 0x80), 0x5F1C40) if i.mnemonic == 'call']
    assert any(int(i.op_str, 16) == 0x5F1B70 for i in caller)
    print('PASS: 1383 per-actor wrapper site: the threat scan LOS call, its argument layout and the ped in ESI match the SA image')


if __name__ == '__main__':
    main(*sys.argv[1:])

"""Publish standard SA mod player globals in the verified native bootstrap.

Only five three-byte variable operands change. Coordinates, densities, clock,
native script chunk sizes, yields and instruction addresses remain untouched.
The independent PowerShell bootstrap validator checks the resulting semantics.
"""
import hashlib
import struct

BINDINGS = ((697, 0, 8), (702, 0, 8), (705, 1, 12), (768, 1, 12), (788, 0, 8))


def export_player_globals(data):
    if len(data) < 802 or data[616:627] != b'\xa4\x03\x09LCSSABT\0':
        raise ValueError('Unknown native bootstrap; player export update refused')
    for offset, opcode in ((675, 0x0053), (700, 0x01f5), (766, 0x00a1), (786, 0x01b4)):
        if struct.unpack_from('<H', data, offset)[0] != opcode:
            raise ValueError('Native bootstrap player instruction changed')
    old = all(data[at:at+3] == struct.pack('<BH', 3, local) for at, local, _ in BINDINGS)
    new = all(data[at:at+3] == struct.pack('<BH', 2, global_offset) for at, _, global_offset in BINDINGS)
    if not old and not new:
        raise ValueError('Mixed/unknown native bootstrap player variable bindings')
    result = bytearray(data)
    for at, _, global_offset in BINDINGS:
        result[at:at+3] = struct.pack('<BH', 2, global_offset)
    return bytes(result)


def report(before, after):
    if export_player_globals(before) != after:
        raise ValueError('Bootstrap changed beyond the reviewed mod player exports')
    return dict(playerIndexByteOffset=8, playerActorByteOffset=12,
                initializedBeforeFirstYield=True, refreshedBeforeNativeScripts=True,
                sourceSha256=hashlib.sha256(before).hexdigest(),
                sha256=hashlib.sha256(after).hexdigest(),
                changedBytes=[i for i, (a, b) in enumerate(zip(before, after)) if a != b],
                gameExecuted=False, modsRuntimeVerified=False)

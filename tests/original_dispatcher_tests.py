"""Exercise the data-only original ELF reader and every retail switch index."""
import importlib.util
import pathlib
import struct
import sys
from types import SimpleNamespace

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('original_inspect', ROOT / 'tools/inspect-original-command.py')
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
image = module.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')


def rejects(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError('Unreviewed or out-of-bounds original data was accepted')


expected = (
    (0, 100, 0x3A6320), (100, 99, 0x3A64B0), (219, 85, 0x3A66C0),
    (309, 96, 0x3B7F40), (405, 100, 0x3B80C0), (505, 100, 0x3BA730),
    (605, 97, 0x3BA8C0), (708, 97, 0x3BAAF0), (805, 100, 0x3B8CD0),
    (905, 100, 0x3B8EA0), (1006, 99, 0x3BB300), (1106, 99, 0x3BB500),
    (1205, 99, 0x3AD230), (1305, 97, 0x3AD520), (1405, 92, 0x3BA140),
    (1497, 103, 0x3BFC90), (1600, 57, 0x3BB920),
)
for opcode in range(1700):
    row = image.handler(opcode)
    group_index = next(i for i, (end, _) in enumerate(module.GROUPS) if opcode < end)
    base, count, table = expected[group_index]
    assert (row['base'], row['count']) == (base, count), row
    assert image.executable(row['handler'])
    if base <= opcode < base + count:
        assert row['table'] == table
        assert row['handler'] == image.word(table + (opcode - base) * 4)
    else:
        assert row['default'] and row['table'] is None

# Boundaries shifted in the retail binary and all three recently ported cases.
assert image.handler(304)['default']
assert image.handler(305)['group'] == 0x208400
assert image.handler(1497)['group'] == 0x2F0DB0
assert image.handler(1600)['group'] == 0x273080
for opcode in (1083, 1111, 1249, 1608):
    assert not image.handler(opcode)['default']
assert image.handler(1608)['handler'] == 0x273280
assert image.read(0x100000, 4) == image.data[0x1000:0x1004]
rejects(lambda: image.read(0x3DA280, 4))  # BSS is not file-backed
rejects(lambda: image.read(0x100000, 0))
rejects(lambda: image.read(-1, 4))
rejects(lambda: image.read(0xffffffff, 4))
rejects(lambda: image.read(0x3865e0, 8))  # cross-section read
for opcode in (-1, 1700, 0x8001):
    rejects(lambda: image.handler(opcode))

def altered(edit):
    data = bytearray(image.data)
    edit(data)
    return module.OriginalImage(SimpleNamespace(read_bytes=lambda: bytes(data)))

rejects(lambda: module.OriginalImage(SimpleNamespace(read_bytes=lambda: b'ELF')))
rejects(lambda: altered(lambda b: b.__setitem__(5, 2)))
rejects(lambda: altered(lambda b: struct.pack_into('<H', b, 18, 62)))
rejects(lambda: altered(lambda b: struct.pack_into('<I', b, 32, len(b))))
rejects(lambda: altered(lambda b: struct.pack_into('<I', b, 2993988 + 40 + 16, len(b))))
rejects(lambda: altered(lambda b: struct.pack_into('<I', b, 2993988 + 40 + 12, 0xfffffffc)))
# A well-formed ELF with a modified instruction still fails the hash profile.
changed = altered(lambda b: b.__setitem__(0x1000, b[0x1000] ^ 1))
rejects(lambda: changed.handler(1))
rejects(lambda: list(changed.instructions(0x100000, 4)))
print('original dispatcher: 1700 bounded indices, 17 tables, shifted boundaries, invalid ELF/profile rejected; gameExecuted=false')

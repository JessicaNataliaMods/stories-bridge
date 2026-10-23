"""Synthetic checks for tools/txd_to_d3d9.py (no game data)."""
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import txd_to_d3d9 as t


def chunk(kind, body, version=0x1003FFFF):
    return struct.pack('<III', kind, len(body), version) + body


def d3d8_texture(name, alpha):
    body = struct.pack('<II', 8, 0x1102) + name.encode().ljust(32, b'\0') + b'\0'*32
    body += struct.pack('<II', 0x0500, alpha) + struct.pack('<HHBBBB', 2, 2, 32, 1, 4, 0)
    body += struct.pack('<I', 16) + bytes(range(16))
    return chunk(0x15, chunk(1, body) + chunk(3, b''))


def dictionary(*textures):
    return chunk(0x16, chunk(1, struct.pack('<HH', len(textures), 0)) + b''.join(textures) + chunk(3, b''))


def fields(data, texture):
    body = texture + 24                 # texture chunk header, struct chunk header
    fixed = body + 8 + 64
    return struct.unpack_from('<I', data, body)[0], struct.unpack_from('<II', data, fixed), data[fixed + 15]


source = dictionary(d3d8_texture('a', 1), d3d8_texture('b', 0))
converted, count = t.convert(source)
assert count == 2 and len(converted) == len(source), 'Conversion keeps the layout and size'
first = 12 + 12 + 4                     # root header, struct chunk header and body
second = first + 12 + struct.unpack_from('<I', source, first + 4)[0]
assert fields(converted, first) == (9, (0x0500, 21), 1), 'Alpha textures become D3D9 A8R8G8B8 with the alpha flag'
assert fields(converted, second) == (9, (0x0500, 22), 0), 'Opaque textures become X8R8G8B8 without it'
for offset in (0, 12, first, first + 12, second):
    assert struct.unpack_from('<I', converted, offset + 8)[0] == t.RW_VERSION, 'Chunk versions are RenderWare 3.6'
assert bytes(range(16)) in converted, 'Pixel data is untouched'
try:
    t.convert(converted)
    raise SystemExit('A D3D9 dictionary has nothing to convert')
except ValueError:
    pass
bad = bytearray(source)
struct.pack_into('<B', bad, first + 24 + 8 + 64 + 8 + 4, 16)       # depth 16
try:
    t.convert(bytes(bad))
    raise SystemExit('Non 32-bit textures must be refused')
except ValueError:
    pass
print('PASS: D3D8 to D3D9 texture dictionary conversion contracts')

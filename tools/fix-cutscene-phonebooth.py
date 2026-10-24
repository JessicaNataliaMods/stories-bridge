"""Raise the cutscene phone booth by the frame offset SA drops.

The LCS cutscene model csphonebooth keeps its geometry 0.698 above the
animated DummyRoot (frame phonebooth1_L0 / Phone_128). In SA the booth ends up
that much too low: its top sits at Toni's hip instead of beside the handset.
Add the offset to every DummyRoot translation key of csphonebooth.

usage: fix-cutscene-phonebooth.py CUTS.IMG OUTPUT.IMG
Only the float payload of matching keys changes; sizes and offsets stay put.
"""
import struct
import sys

OFFSET = 0.698
ANIMATION = 'csphonebooth'
BONE = 'DummyRoot'


def padded(size):
    return (size + 3) & ~3


def patch_block(block):
    """Return (patched block, number of keys changed)."""
    data = bytearray(block)
    if data[:4] != b'ANPK' or data[8:12] != b'INFO':
        return block, 0
    changed = 0
    cursor = 8
    size, count = struct.unpack_from('<Ii', data, cursor + 4)
    cursor += 8 + padded(size)
    for _ in range(count):
        size = struct.unpack_from('<I', data, cursor + 4)[0]
        name = bytes(data[cursor + 8:cursor + 8 + size]).split(b'\0')[0].decode('latin1')
        cursor += 8 + padded(size)
        end = cursor + 8 + struct.unpack_from('<I', data, cursor + 4)[0]
        cursor += 8
        size, objects = struct.unpack_from('<Ii', data, cursor + 4)
        cursor += 8 + padded(size)
        for _ in range(objects):
            cursor += 8
            size = struct.unpack_from('<I', data, cursor + 4)[0]
            body = data[cursor + 8:cursor + 8 + size]
            bone = bytes(body[:28]).split(b'\0')[0].decode('latin1')
            frames = struct.unpack_from('<i', body, 28)[0]
            cursor += 8 + padded(size)
            kind = bytes(data[cursor:cursor + 4])
            size = struct.unpack_from('<I', data, cursor + 4)[0]
            per = {b'KR00': 5, b'KRT0': 8, b'KRTS': 11}[kind]
            if name == ANIMATION and bone == BONE and kind == b'KRT0':
                for i in range(frames):
                    at = cursor + 8 + 4 * per * i + 4 * 6  # quaternion(4), x, y, then z
                    z = struct.unpack_from('<f', data, at)[0]
                    struct.pack_into('<f', data, at, z + OFFSET)
                    changed += 1
            cursor += 8 + padded(size)
        cursor = end
    return bytes(data), changed


def main(source, target):
    image = bytearray(open(source, 'rb').read())
    if image[:4] != b'VER2':
        raise SystemExit('Not an IMG v2 archive')
    total = 0
    for index in range(struct.unpack_from('<I', image, 4)[0]):
        base = 8 + 32 * index
        offset, size, size2 = struct.unpack_from('<IHH', image, base)
        name = bytes(image[base + 8:base + 32]).split(b'\0')[0].decode('latin1')
        if not name.lower().endswith('.ifp'):
            continue
        start, length = offset * 2048, (size or size2) * 2048
        patched, changed = patch_block(bytes(image[start:start + length]))
        if changed:
            image[start:start + length] = patched
            total += changed
            print('%s: %d keys raised by %.3f' % (name, changed, OFFSET))
    open(target, 'wb').write(image)
    print('total keys:', total)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])

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


LIMIT = 31.9  # SA stores compressed translations as int16 / 1024 (about +-32 m)


def large_animations(block):
    """Animation names whose translation does not fit SA's compressed keys."""
    names = set()
    if block[:4] != b'ANPK' or block[8:12] != b'INFO':
        return names
    cursor = 8
    size, count = struct.unpack_from('<Ii', block, cursor + 4)
    cursor += 8 + padded(size)
    for _ in range(count):
        size = struct.unpack_from('<I', block, cursor + 4)[0]
        name = bytes(block[cursor + 8:cursor + 8 + size]).split(b'\0')[0].decode('latin1')
        cursor += 8 + padded(size)
        end = cursor + 8 + struct.unpack_from('<I', block, cursor + 4)[0]
        cursor += 8
        size, objects = struct.unpack_from('<Ii', block, cursor + 4)
        cursor += 8 + padded(size)
        for _ in range(objects):
            cursor += 8
            size = struct.unpack_from('<I', block, cursor + 4)[0]
            frames = struct.unpack_from('<i', block, cursor + 8 + 28)[0]
            cursor += 8 + padded(size)
            kind = bytes(block[cursor:cursor + 4])
            size = struct.unpack_from('<I', block, cursor + 4)[0]
            per = {b'KR00': 5, b'KRT0': 8, b'KRTS': 11}[kind]
            if kind != b'KR00':
                for i in range(frames):
                    x, y, z = struct.unpack_from('<3f', block, cursor + 8 + 4 * per * i + 16)
                    if max(abs(x), abs(y), abs(z)) >= LIMIT:
                        names.add(name)
            cursor += 8 + padded(size)
        cursor = end
    return names


def add_uncompressed(cut, names):
    """List the animations in the .cut uncompress section (kept as floats)."""
    text = cut.split(b'\0')[0]
    marker = b'uncompress\r\n'
    at = text.find(marker)
    if at < 0:
        raise SystemExit('.cut has no uncompress section')
    end = at + len(marker)
    listed = text[end:text.index(b'end', end)].split()
    add = b''.join(n.encode('latin1') + b'\r\n' for n in sorted(names) if n.encode('latin1') not in listed)
    patched = text[:end] + add + text[end:]
    if len(patched) > len(cut):
        raise SystemExit('.cut does not fit its sector padding')
    return patched + cut[len(patched):]


def main(source, target):
    image = bytearray(open(source, 'rb').read())
    if image[:4] != b'VER2':
        raise SystemExit('Not an IMG v2 archive')
    total = 0
    entries = {}
    for index in range(struct.unpack_from('<I', image, 4)[0]):
        base = 8 + 32 * index
        offset, size, size2 = struct.unpack_from('<IHH', image, base)
        name = bytes(image[base + 8:base + 32]).split(b'\0')[0].decode('latin1')
        entries[name.lower()] = (offset * 2048, (size or size2) * 2048)
    for name, (start, length) in entries.items():
        if not name.endswith('.ifp'):
            continue
        patched, changed = patch_block(bytes(image[start:start + length]))
        if changed:
            image[start:start + length] = patched
            total += changed
            print('%s: %d keys raised by %.3f' % (name, changed, OFFSET))
        large = large_animations(bytes(image[start:start + length]))
        if large:
            cut_start, cut_length = entries[name[:-4] + '.cut']
            image[cut_start:cut_start + cut_length] = add_uncompressed(bytes(image[cut_start:cut_start + cut_length]), large)
            print('%s: uncompressed %s' % (name[:-4] + '.cut', ', '.join(sorted(large))))
    open(target, 'wb').write(image)
    print('total keys:', total)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])

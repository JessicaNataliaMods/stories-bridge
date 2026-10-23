"""Convert a RenderWare 3.1 D3D8 texture dictionary (the converter's PC output) to the D3D9 layout
that SA's RenderWare reads. SA cannot load platform-8 native textures; the package's own D3D9
textures (weapons/*.txd) use the layout written here.

Only uncompressed 32-bit textures are handled, which is what the LCS HUD and font dictionaries
use; anything else is refused instead of guessed. Pure byte transform on the user's own files.
"""
import struct
import sys

RW_VERSION = 0x1803FFFF   # RenderWare 3.6.0.3, as in SA's own dictionaries
ID_STRUCT, ID_EXTENSION, ID_TEXTURE_NATIVE, ID_TEXDICT = 0x01, 0x03, 0x15, 0x16
D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8 = 21, 22
NAME, MASK = 32, 32


def convert(data):
    out = bytearray(data)
    state = {'textures': 0}

    def walk(offset, end):
        while offset + 12 <= end:
            kind, size, _ = struct.unpack_from('<III', out, offset)
            struct.pack_into('<I', out, offset + 8, RW_VERSION)
            body = offset + 12
            if body + size > end:
                raise ValueError('Chunk runs past its parent at 0x%x' % offset)
            if kind in (ID_TEXDICT, ID_TEXTURE_NATIVE):
                if kind == ID_TEXTURE_NATIVE:
                    texture_struct(body)
                walk(body, body + size)
            offset = body + size

    def texture_struct(parent):
        kind, size, _ = struct.unpack_from('<III', out, parent)
        if kind != ID_STRUCT:
            raise ValueError('Texture without a struct chunk')
        base = parent + 12
        platform, = struct.unpack_from('<I', out, base)
        if platform == 9:
            return                      # already D3D9
        if platform != 8:
            raise ValueError('Unsupported texture platform %d' % platform)
        fixed = base + 8 + NAME + MASK
        raster_format, alpha = struct.unpack_from('<II', out, fixed)
        width, height, depth, mips, raster_type, compression = struct.unpack_from('<HHBBBB', out, fixed + 8)
        if depth != 32 or compression != 0 or (raster_format & 0xF000) or (raster_format & 0x0F00) not in (0x0500, 0x0600):
            raise ValueError('Only uncompressed 32-bit D3D8 textures are supported (format 0x%x depth %d)' % (raster_format, depth))
        struct.pack_into('<I', out, base, 9)
        struct.pack_into('<I', out, fixed + 4, D3DFMT_A8R8G8B8 if alpha else D3DFMT_X8R8G8B8)
        struct.pack_into('<B', out, fixed + 15, 1 if alpha else 0)
        state['textures'] += 1

    walk(0, len(out))
    if not state['textures']:
        raise ValueError('No D3D8 textures to convert')
    return bytes(out), state['textures']


def main(argv):
    if len(argv) != 3:
        raise SystemExit('usage: txd_to_d3d9.py <input.txd> <output.txd>')
    data = open(argv[1], 'rb').read()
    converted, count = convert(data)
    if len(converted) != len(data):
        raise ValueError('Conversion changed the file size')
    open(argv[2], 'wb').write(converted)
    print('converted %d textures' % count)


if __name__ == '__main__':
    main(sys.argv)

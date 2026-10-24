"""Check the user-files native interfaces as PE data; never execute the game."""
import argparse
import json
import struct
from pathlib import Path


def validate(executable):
    data = Path(executable).read_bytes()
    if data[:2] != b'MZ':
        raise ValueError('Expected a PE executable')
    pe, = struct.unpack_from('<I', data, 0x3c)
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size, = struct.unpack_from('<H', data, pe+20)
    optional = pe+24
    magic, = struct.unpack_from('<H', data, optional)
    base, = struct.unpack_from('<I', data, optional+28)
    if (machine, magic, base) != (0x14c, 0x10b, 0x400000):
        raise ValueError('Unsupported native address model')
    sections = []
    for index in range(count):
        row = optional+optional_size+index*40
        virtual_size, rva, raw_size, offset = struct.unpack_from('<4I', data, row+8)
        flags, = struct.unpack_from('<I', data, row+36)
        sections.append((base+rva, virtual_size, raw_size, offset, flags))

    def section(address, size, flag):
        for start, virtual_size, raw_size, offset, flags in sections:
            if start <= address and address+size <= start+max(virtual_size, raw_size) and flags & flag:
                return offset+address-start, raw_size-(address-start)
        raise ValueError('Native interface outside the expected PE section')

    def code(address, size):
        offset, available = section(address, size, 0x20000000)
        if available < size:
            raise ValueError('Native instruction bytes are not present in the file')
        return data[offset:offset+size]

    entry = code(0x744fb0, 8)
    if entry != bytes.fromhex('83 ec 0c 8d 44 24 00 50'):
        raise ValueError('GetUserFilesDir entry is already replaced or incompatible')
    call = code(0x748739, 10)
    if call[0] != 0xe8 or call[5:] != bytes.fromhex('83 c4 08 85 c0'):
        raise ValueError('Early startup event has incompatible stack/return conventions')
    target = 0x748739+5+struct.unpack_from('<i', call, 1)[0]
    section(target, 1, 0x20000000)
    for address in (0xc92168, 0xc92268):
        section(address, 256, 0x80000000)
    return dict(valid=True, earlyEventCall=True, nativeDirectoryEntry=True,
                tracksAndGalleryWritable=True, wholeFileHashRequired=False,
                gameExecuted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = validate(args.executable)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PASS: user-files startup CALL, native entry and writable buffers; PE data only')

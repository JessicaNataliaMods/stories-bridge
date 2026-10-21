"""Inspect the on-disk SA task vtables; never load or execute the game."""
import pathlib
import struct
import sys

binary = pathlib.Path(sys.argv[1]).read_bytes()
nt = struct.unpack_from('<I', binary, 60)[0]
sections = nt + 24 + struct.unpack_from('<H', binary, nt + 20)[0]
spans = [struct.unpack_from('<IIII', binary, sections+i*40+8)
         for i in range(struct.unpack_from('<H', binary, nt+6)[0])]

def address(offset):
    return next((0x400000+rva+offset-raw for _,rva,length,raw in spans
                 if raw <= offset < raw+length), None)

start = 0
while True:
    offset = binary.find(bytes.fromhex('B8 8B 03 00 00 C3'), start)
    if offset < 0:
        break
    function = address(offset)
    start = offset+1
    cursor = 0
    while True:
        cursor = binary.find(struct.pack('<I', function), cursor)
        if cursor < 0:
            break
        table = address(cursor-16)
        if table and 0x850000 < table < 0x880000:
            entries = struct.unpack_from('<11I', binary, cursor-16)
            print(f'seekTable=0x{table:X} type=0x{function:X} first=0x{entries[9]:X} control=0x{entries[10]:X}')
        cursor += 4

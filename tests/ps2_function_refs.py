"""All references to a PS2 function entry: jal/j targets in .text and 32-bit words in every loadable segment
(function-pointer tables, vtables) plus lui/addiu|ori address construction (jalr callers).
Usage: ps2_function_refs.py <SLUS_214.23> <entry> [<entry> ...]   Data only; nothing is executed."""
import struct, sys
elf = open(sys.argv[1], 'rb').read()
phoff, = struct.unpack_from('<I', elf, 0x1C)
phentsize, phnum = struct.unpack_from('<HH', elf, 0x2A)
segs = []
for i in range(phnum):
    t, o, v, _, fsz, msz, fl, _ = struct.unpack_from('<IIIIIIII', elf, phoff + i * phentsize)
    if t == 1:
        segs.append((v, o, fsz, fl))
for target in (int(x, 0) for x in sys.argv[2:]):
    jal = j = data = built = 0
    where = []
    for v, o, fsz, fl in segs:
        words = struct.unpack_from('<%dI' % (fsz // 4), elf, o)
        luis = {}
        for k, w in enumerate(words):
            ad = v + 4 * k
            if (fl & 1):
                if w == (3 << 26) | (target >> 2) : jal += 1; where.append(('jal', ad))
                if w == (2 << 26) | (target >> 2) : j += 1; where.append(('j', ad))
                op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
                if op == 0x0F: luis[rt] = (k, imm)
                elif op in (9, 0x0D) and rs in luis and k - luis[rs][0] <= 8:
                    s = imm - 0x10000 if (op == 9 and imm & 0x8000) else imm
                    if ((luis[rs][1] << 16) + s) & 0xFFFFFFFF == target: built += 1; where.append(('addr', ad))
            if w == target: data += 1; where.append(('word', ad))
    print('%#x: jal=%d j=%d address-built=%d data-words=%d %s' % (target, jal, j, built, data, [(a, hex(b)) for a, b in where]))

"""Exhaustive PS2 .text scan for accesses to one bit of a struct byte (1593: CPed byte 0x1A4 bit 0x04; 1431: gang array).

Usage: ps2_flag_access_scan.py <SLUS_214.23> byte <offset> <mask> [--list]
       ps2_flag_access_scan.py <SLUS_214.23> abs <lo> <hi> [--list]

byte mode: every load whose effective offset covers the byte (lb/lbu @off, lh/lhu @off&~1, lw @off&~3) is followed
over the next 14 straight instructions; a use is counted when the loaded register meets `andi` with a mask that has
the target bit (shifted by the byte position inside the loaded unit) or is shifted/extended into a bit test. Stores
(sb/sh/sw at the same offsets) are reported as writers. Every hit is printed with its enclosing function entry, so the
base register can be reviewed by hand; the output is data only (nothing is executed).
abs mode: every lui/imm pair that builds an address in [lo, hi] is reported with load/store kind (static arrays).
Limits: linear scan, bases are not typed; the result is a candidate list, not proof by itself.
"""
import struct
import sys

LB, LBU, LH, LHU, LW, LWU, LD = 0x20, 0x24, 0x21, 0x25, 0x23, 0x27, 0x37
SB, SH, SW, SD = 0x28, 0x29, 0x2B, 0x3F


def load(path):
    elf = open(path, 'rb').read()
    phoff, = struct.unpack_from('<I', elf, 0x1C)
    _, off, va, _, fsz, _, _, _ = struct.unpack_from('<IIIIIIII', elf, phoff)
    return va, struct.unpack_from('<%dI' % (fsz // 4), elf, off)


def function_start(words, va, index):
    # prologue heuristic: nearest previous `addiu sp, sp, -N`
    for j in range(index, max(index - 4000, 0), -1):
        w = words[j]
        if (w >> 26) == 9 and ((w >> 21) & 31) == 29 and ((w >> 16) & 31) == 29 and (w & 0x8000):
            return va + 4 * j
    return 0


def byte_mode(va, words, off, mask, listing):
    unit = {LB: (1, 0), LBU: (1, 0), LH: (2, off & 1), LHU: (2, off & 1), LW: (4, off & 3), LWU: (4, off & 3), LD: (8, off & 7)}
    loads, stores, uses = [], [], []
    for i, w in enumerate(words):
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op in unit:
            size, pos = unit[op]
            if simm == off - pos:
                loads.append((va + 4 * i, op, rt, rs))
                shifted = mask << (8 * pos)
                for j in range(i + 1, min(i + 15, len(words))):
                    n = words[j]
                    nop, nrs, nrt = n >> 26, (n >> 21) & 31, (n >> 16) & 31
                    if nop == 0x0C and nrs == rt and (n & 0xFFFF) & shifted:      # andi
                        uses.append((va + 4 * i, va + 4 * j, 'andi %#x' % (n & 0xFFFF), rs))
                        break
                    if nop == 0 and (n & 0x3F) in (0, 2, 3) and ((n >> 16) & 31) == rt:   # sll/srl/sra on the loaded reg
                        sh = (n >> 6) & 31
                        uses.append((va + 4 * i, va + 4 * j, 'shift %d' % sh, rs))
                        break
                    if (nop == 0x1F and nrs == rt):                                # ext/ins family (R5900 has none; keep for safety)
                        uses.append((va + 4 * i, va + 4 * j, 'ext', rs))
                        break
        if op in (SB, SH, SW, SD):
            size = {SB: 1, SH: 2, SW: 4, SD: 8}[op]
            if simm <= off < simm + size:
                stores.append((va + 4 * i, op, rt, rs))
    # Read-modify-write: a store to the same base register and covering offset follows the load within 40
    # instructions with the base unchanged. Those are writers of other bits of the unit, not bit readers.
    rmw, readers = [], []
    for a, lop, lrt, base in loads:
        i = (a - va) // 4
        simm0 = (words[i] & 0xFFFF) - ((words[i] & 0x8000) << 1)
        found = False
        for j in range(i + 1, min(i + 41, len(words))):
            n = words[j]
            nop, nrs, nrt = n >> 26, (n >> 21) & 31, (n >> 16) & 31
            if nop in (SB, SH, SW, SD) and nrs == base:
                ssz = {SB: 1, SH: 2, SW: 4, SD: 8}[nop]
                nsimm = (n & 0xFFFF) - ((n & 0x8000) << 1)
                if nsimm <= off < nsimm + ssz:
                    found = True
                    break
            if nop in (9, 0x19) and nrt == base:       # base reassigned
                break
        (rmw if found else readers).append((a, lop, lrt, base))
    print('read-modify-write loads (writer-side):', len(rmw), '| loads with no matching store (reader candidates):', len(readers))
    for a, lop, lrt, base in readers:
        print('  CANDIDATE %#x op=%#x dst=$%d base=$%d fn=%#x' % (a, lop, lrt, base, function_start(words, va, (a - va) // 4)))
    print('byte offset %#x mask %#x' % (off, mask))
    print('covering loads:', len(loads), '| bit-test uses:', len(uses), '| covering stores:', len(stores))
    funcs = sorted({function_start(words, va, (a - va) // 4) for a, *_ in loads})
    print('functions with a covering load:', len(funcs))
    for a, op, rt, base in loads:
        print('  LOAD  %#x op=%#x dst=$%d base=$%d fn=%#x' % (a, op, rt, base, function_start(words, va, (a - va) // 4)))
    for a, b, k, base in uses:
        print('  USE  load %#x test %#x %s base=$%d fn=%#x' % (a, b, k, base, function_start(words, va, (a - va) // 4)))
    for a, op, rt, base in stores:
        print('  STORE %#x op=%#x src=$%d base=$%d fn=%#x' % (a, op, rt, base, function_start(words, va, (a - va) // 4)))
    return loads, stores, uses


def abs_mode(va, words, lo, hi):
    luis, hits = {}, []
    for i, w in enumerate(words):
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op == 0x0F:
            luis[rt] = (i, imm)
            continue
        if rs in luis and i - luis[rs][0] <= 24 and op in (9, 0x0D, LB, LBU, LH, LHU, LW, LWU, SB, SH, SW, 0x37, 0x3F, 0x31, 0x39):
            simm = imm - 0x10000 if imm & 0x8000 else imm
            addr = ((luis[rs][1] << 16) + simm) & 0xFFFFFFFF
            if lo <= addr <= hi:
                hits.append((va + 4 * i, addr, op))
    print('absolute references to [%#x,%#x]: %d' % (lo, hi, len(hits)))
    for a, addr, op in hits:
        print('  %#x addr=%#x op=%#x fn=%#x' % (a, addr, op, function_start(words, va, (a - va) // 4)))
    return hits


if __name__ == '__main__':
    va, words = load(sys.argv[1])
    if sys.argv[2] == 'ld64':
        pass
    elif sys.argv[2] == 'byte':
        byte_mode(va, words, int(sys.argv[3], 0), int(sys.argv[4], 0), '--list' in sys.argv)
    else:
        abs_mode(va, words, int(sys.argv[3], 0), int(sys.argv[4], 0))


def ld64_bit_readers(va, words, off, bit):
    """Doubleword loads at `off` followed (<= 10 instrs, same destination register still live) by an isolation of `bit`:
    right shift to bit 0 (dsrl/dsrl32/dsra/dsra32), left shift to bit 63, or AND with a 64-bit constant built with
    dsll32 of a small immediate. Returns [(load_pc, pattern, base_reg)]."""
    out = []
    for i, w in enumerate(words):
        if (w >> 26) != 0x37 or ((w & 0xFFFF) - ((w & 0x8000) << 1)) != off:
            continue
        dst, base = (w >> 16) & 31, (w >> 21) & 31
        consts = {}
        for j in range(i + 1, min(i + 11, len(words))):
            n = words[j]
            nop, nrs, nrt, nrd, sa, fn = n >> 26, (n >> 21) & 31, (n >> 16) & 31, (n >> 11) & 31, (n >> 6) & 31, n & 0x3F
            if nop in (9, 0x0D) and nrs == 0:
                consts[nrt] = n & 0xFFFF
            if nop == 0 and nrt == dst:
                if fn in (0x3A, 0x3B) and sa == bit and bit < 32: out.append((va + 4 * i, 'dsrl/dsra %d' % sa, base))
                if fn in (0x3E, 0x3F) and 32 + sa == bit: out.append((va + 4 * i, 'dsrl32/dsra32 %d' % sa, base))
                if fn == 0x38 and sa == 63 - bit and bit >= 32: out.append((va + 4 * i, 'dsll %d' % sa, base))
            if nop == 0 and fn in (0x38, 0x3C) and nrt in consts and isinstance(consts[nrt], int) and                (consts[nrt] << ((32 if fn == 0x3C else 0) + sa)) == (1 << bit):      # dsll / dsll32 builds the mask
                consts[nrd] = ('mask', 1 << bit)
            if nop == 0 and fn == 0x24 and dst in (nrs, nrt) and any(isinstance(consts.get(r), tuple) for r in (nrs, nrt)):
                out.append((va + 4 * i, 'and with 64-bit mask', base))
            if nop == 0x0C and nrs == dst and bit < 16 and (n & 0xFFFF) & (1 << bit):
                out.append((va + 4 * i, 'andi', base))
    return out


if __name__ == '__main__' and sys.argv[2] == 'ld64':
    va, words = load(sys.argv[1])
    hits = ld64_bit_readers(va, words, int(sys.argv[3], 0), int(sys.argv[4], 0))
    print('ld64 bit readers off=%s bit=%s: %d' % (sys.argv[3], sys.argv[4], len(hits)))
    for a, k, b in hits:
        print('  READ %#x %s base=$%d fn=%#x' % (a, k, b, function_start(words, va, (a - va) // 4)))

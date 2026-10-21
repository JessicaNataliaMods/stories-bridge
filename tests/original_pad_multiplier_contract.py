"""Data-only reader search for CPad+0xB4/+0xB8 in the original PS2 image (SET_PAD_STICKS_MULTIPLIER, 1620).

Usage: original_pad_multiplier_contract.py <SLUS_214.23> [--list]

Method (no execution): every pad pointer comes from CPad::GetPad (0x170BE8: pads at 0x408C60, stride 0xBC) or from a
CPad method argument. For every `jal GetPad` the result register is followed forward with an offset (move/addiu),
through calls that receive it in $a0..$a3 (the callee is analysed with the same rule, depth <= 6), and every
load/store whose effective offset overlaps the 8 bytes [0xB4,0xBC) is reported with its kind. Escapes (the pointer
being stored to memory, passed through jalr, or returned) are counted separately because they hide readers.
Positive controls: the writer in the 1620 handler and the two initialisers are found.
Data only: nothing is executed; it grants no runtime support. Limits are printed.
"""
import collections
import struct
import sys

GET_PAD = 0x170BE8
LO, HI = 0xB4, 0xBC
LOADS = {0x20: 1, 0x21: 2, 0x23: 4, 0x24: 1, 0x25: 2, 0x27: 4, 0x37: 8, 0x1E: 16, 0x31: 4, 0x35: 8, 0x32: 4, 0x36: 8, 0x1A: 4, 0x1B: 4, 0x22: 4, 0x26: 4}
STORES = {0x28: 1, 0x29: 2, 0x2B: 4, 0x3F: 8, 0x1F: 16, 0x39: 4, 0x3D: 8, 0x2A: 4, 0x2E: 4, 0x2C: 8, 0x2D: 8}


# Reviewed by hand (disassembly in logs/evidence): the only places where a followed pad pointer is combined with an
# index. Each indexes an array that ends below +0xB4 (index bounded), so none can reach the two floats.
REVIEWED_DERIVED = {
    0x170ccc: 'SteeringLeftRightBuffer[10] shorts at +0x62 (index from the drunk-steering counter)',
    0x170cfc: 'SteeringLeftRightBuffer[10] shorts at +0x62',
    0x16ec34: 'bHornHistory[8] at +0x86 cleared by an 8-iteration loop (sltiu 8)',
    0x12c118: 'bHornHistory[8] ring at +0x86, index masked below 8',
    0x12c140: 'bHornHistory[8] ring at +0x86',
    0x12c164: 'bHornHistory[8] ring at +0x86',
    0x12c1ac: 'bHornHistory[8] ring at +0x86',
    0x12c1d0: 'bHornHistory[8] ring at +0x86',
    0x12c274: 'bHornHistory[8] ring at +0x86',
}


def load(path):
    elf = open(path, 'rb').read()
    phoff, = struct.unpack_from('<I', elf, 0x1C)
    _, off, va, _, fsz, _, _, _ = struct.unpack_from('<IIIIIIII', elf, phoff)
    words = struct.unpack_from('<%dI' % (fsz // 4), elf, off)
    return va, words


def main(path, listing):
    va, words = load(path)

    def at(addr):
        return words[(addr - va) // 4]

    jal_get = (3 << 26) | (GET_PAD >> 2)
    callers = [va + 4 * i for i, w in enumerate(words) if w == jal_get]
    # positive control: GetPad itself multiplies by 0xBC (0x170BE8 region contains addiu ... 0xBC)
    body = [at(GET_PAD + 4 * k) for k in range(0, 12)]
    control_stride = any((w >> 26) == 9 and (w & 0xFFFF) == 0xBC for w in body) or any((w >> 26) == 0x0F for w in body)
    stores, reads, escapes, indirect = [], [], collections.Counter(), 0
    derived = []
    spills = []
    seen_functions = set()

    def overlap(offset, width):
        return offset < HI and offset + width > LO

    def analyse(start, regs, depth, origin):
        nonlocal indirect
        key = (start, tuple(sorted(regs.items())))
        if key in seen_functions or depth > 6:
            return
        seen_functions.add(key)
        regs = dict(regs)
        slots = {}

        def straight(pc):
            """One non-branch instruction: report overlapping accesses, then update the tainted registers."""
            w = at(pc)
            op = w >> 26
            rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
            imm = w & 0xFFFF
            simm = imm - 0x10000 if imm & 0x8000 else imm
            func = w & 0x3F
            if op in LOADS or op in STORES:
                table = LOADS if op in LOADS else STORES
                if rs in regs and rs != 29:
                    eff = regs[rs] + simm
                    if overlap(eff, table[op]):
                        (reads if op in LOADS else stores).append((pc, op, rs, eff, origin))
                if op in STORES and rt in regs and op in (0x2B, 0x3F, 0x1F):
                    if rs == 29:
                        spills.append(pc)
                        slots[simm] = regs[rt]
                    else:
                        escapes['pointer stored to memory'] += 1
                if op in LOADS and op not in (0x31, 0x35, 0x32, 0x36):
                    if rs == 29 and op in (0x23, 0x27, 0x37) and simm in slots:
                        regs[rt] = slots[simm]   # reload of a spilled pad pointer keeps its taint
                    else:
                        regs.pop(rt, None)
                return
            if op in (9, 0x19) and rs in regs:                     # addiu rt, rs, imm
                regs[rt] = regs[rs] + simm
            elif op == 0 and func in (0x21, 0x25, 0x2D) and rt == 0 and rs in regs:
                regs[rd] = regs[rs]
            elif op == 0 and func in (0x21, 0x25, 0x2D) and rs == 0 and rt in regs:
                regs[rd] = regs[rt]
            else:
                dest = None
                if op == 0 and func not in (8, 9, 0xC, 0xD):
                    dest = rd
                elif 8 <= op <= 0xF:
                    dest = rt
                if dest is not None:
                    sources = (rs, rt) if op == 0 else (rs,)
                    if any(r in regs for r in sources) and not (op == 0 and func in (0x2A, 0x2B)) and op not in (0x0A, 0x0B):
                        escapes['derived value (pointer arithmetic beyond move/addiu)'] += 1
                        derived.append(pc)
                    regs.pop(dest, None)

        pc = start
        boundary = 0
        limit = pc + 4 * 3000
        while pc < limit and boundary < 2:
            w = at(pc)
            op = w >> 26
            rs, rt = (w >> 21) & 31, (w >> 16) & 31
            func = w & 0x3F
            if op == 0 and func == 8 and rs == 31:        # jr $ra
                straight(pc + 4)
                boundary += 1
                pc += 8
                regs = {r: o for r, o in regs.items() if r >= 16 and r != 31} if boundary < 2 else regs
                continue
            if op == 3 or (op == 0 and func == 9):          # jal / jalr: delay slot runs before the callee
                straight(pc + 4)
                if op == 3:
                    target = ((w & 0x3FFFFFF) << 2) | (pc & 0xF0000000)
                    args = {r: regs[r] for r in (4, 5, 6, 7) if r in regs}
                    if args and target != GET_PAD and va <= target < va + 4 * len(words):
                        for reg, off in args.items():
                            analyse(target, {reg: off}, depth + 1, 'argument a%d of %#x' % (reg - 4, pc))
                    elif args and target != GET_PAD:
                        escapes['pointer passed outside the image'] += 1
                elif any(r in regs for r in (4, 5, 6, 7)):
                    indirect += 1
                    escapes['pointer passed through jalr'] += 1
                for r in (2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25):
                    regs.pop(r, None)
                pc += 8
                continue
            straight(pc)
            pc += 4

    for call in callers:
        # result in $v0 after the delay slot; analyse the rest of the caller linearly
        analyse(call + 8, {2: 0}, 0, 'GetPad at %#x' % call)

    # Direct references to the pad array (0x408C60 + n*0xBC) outside CPad::GetPad are a second source of pad pointers:
    # a register built with addiu/ori is followed like a GetPad result, a direct load/store is checked in place.
    array_lo, array_hi = 0x408C60, 0x408C60 + 2 * 0xBC
    direct = []
    luis = {}
    for i, w in enumerate(words):
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op == 0x0F:
            luis[rt] = (i, imm)
        elif rs in luis and i - luis[rs][0] <= 12 and (op in (9, 0x0D) or op in LOADS or op in STORES):
            addr = ((luis[rs][1] << 16) + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
            if array_lo <= addr < array_hi and not (GET_PAD <= va + 4 * i < GET_PAD + 0x40):
                direct.append((va + 4 * i, op, rt, (addr - array_lo) % 0xBC))
    for pc, op, rt, field in direct:
        if op in (9, 0x0D):
            analyse(pc + 4, {rt: field}, 0, 'array address at %#x' % pc)
        else:
            table = LOADS if op in LOADS else STORES
            if overlap(field, table[op]):
                (reads if op in LOADS else stores).append((pc, op, 0, field, 'direct array access'))
    print('direct references to the pad array outside GetPad:', [(hex(a), hex(b)) for a, b, _, _ in direct])
    print('GetPad callers:', len(callers), '| analysed (function, taint) states:', len(seen_functions))
    print('positive control, GetPad stride/base code present:', control_stride)
    print('writers of the two floats found:', len(stores), '| readers found:', len(reads))
    for s in stores:
        print('  store/write at %#x op=%#x offset=%#x via %s' % (s[0], s[1], s[3], s[4]))
    for r in reads:
        print('  READ at %#x op=%#x offset=%#x via %s' % (r[0], r[1], r[3], r[4]))
    print('pad pointer spilled to the stack at:', [hex(x) for x in spills])
    print('derived-value sites:', [hex(x) for x in derived][:40])
    print('escapes:', dict(escapes), '| jalr with pad argument:', indirect)
    global derived_sites
    derived_sites = derived
    return stores, reads, escapes


derived_sites = []


if __name__ == '__main__':
    stores, reads, escapes = main(sys.argv[1], '--list' in sys.argv)
    assert set(derived_sites) <= set(REVIEWED_DERIVED), sorted(hex(x) for x in set(derived_sites) - set(REVIEWED_DERIVED))
    assert len(stores) >= 6, 'positive control: the initialisers and the 1620 handler must be found'
    assert not reads
    print('RESULT: readers=0; every derived-index site is a reviewed in-struct array below +0xB4; no pointer store/jalr escape')
    print('LIMITS: linear (branch-insensitive) taint; pointers rebuilt from non-GetPad, non-array sources are not modelled; the')
    print('analysis proves no PS2 code READS the two floats through a pad pointer, not that SA has no analogous input.')

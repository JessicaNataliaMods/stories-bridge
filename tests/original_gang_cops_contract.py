"""Reproducible contract for SET_GANG_ATTACK_PLAYER_WITH_COPS (1431) and SET_CHAR_ATTACKS_PLAYER_WITH_COPS (1593).

Usage: original_gang_cops_contract.py <SLUS_214.23>      (data only; reads the PS2 ELF, executes nothing)

Facts checked mechanically on the retail image:
 1431  gang array 0x398750 (9 words, index = ped type - 7) is referenced only by the setter (0x2D2208, one caller, the
       script command) and the getter (entry 0x2D2230, three callers). Each getter caller is a ped *spawn* function and
       copies the answer into CPed byte 0x1A1 bit 0 (flag word 0x1A0 |= 0x100) right after creating the ped.
       The only readers of that flag (64-bit load at 0x1A0 tested with 0x100) are the two civilian threat-reaction
       branches of function 0x2661c8 (player threat + gang member + flag -> kill instead of flee-from-cops).
 1593  the per-ped flag (CPed byte 0x1A4 bit 0x04 = doubleword 0x1A0 bit 34) has exactly one reader, 0x31CE38 in 0x31CC18:
       with the player's cop counter (+0x808) non-zero and the flag clear it calls 0x3081A0(ped, 3) (the flee-from-cops
       objective); the retail code only reaches it for released (non-mission) peds (Opus16 note, flow in that delivery).
       The only narrow-load candidate (0x220188) is a bounds-checked jump-table index, not a bit test.
Positive controls: the same detector finds the 1431 readers (andi mask) and the 1593 reader (dsll-built 64-bit mask).
Limits: linear scan; bases are untyped (offsets 0x1A0/0x1A4 exist in other structures; every candidate was reviewed in
logs/evidence/c1593-byte1A4.txt); a reader that moves the flag word through memory or a vector register is not modelled.
It supports adaptation decisions only; it grants no runtime support.
"""
import sys
import ps2_flag_access_scan as scan

SETTER, GETTER = 0x2D2208, 0x2D2230
SPAWN_CALLERS = (0x2F007C, 0x2F0220, 0x2F075C)
THREAT_FN = 0x2661C8


def main(path):
    va, words = scan.load(path)

    def at(a):
        return words[(a - va) // 4]

    def jal_sites(target):
        word = (3 << 26) | (target >> 2)
        return [va + 4 * i for i, w in enumerate(words) if w == word]

    assert jal_sites(SETTER) == [0x24EBE8], jal_sites(SETTER)
    assert jal_sites(GETTER) == list(SPAWN_CALLERS), [hex(x) for x in jal_sites(GETTER)]
    refs = scan.abs_mode(va, words, 0x398750, 0x398773)
    funcs = {scan.function_start(words, va, (a - va) // 4) for a, _, _ in refs}
    assert funcs == {0x2D21E8}, [hex(f) for f in funcs]          # setter and getter share one 0x2D21E8 object range
    for call in SPAWN_CALLERS:
        # call+8 (after the delay slot): beq v0,zero over `ld 0x1a0 ; or 0x100 ; sd 0x1a0`
        assert (at(call + 8) >> 26) == 4 and ((at(call + 8) >> 21) & 31) == 2, hex(call)
        ld, orw, sd = at(call + 16), at(call + 20), at(call + 24)
        assert (ld >> 26) == 0x37 and (ld & 0xFFFF) == 0x1A0, hex(call)
        assert (sd >> 26) == 0x3F and (sd & 0xFFFF) == 0x1A0, hex(call)
        assert (at(call + 12) & 0xFFFF) == 0x100, hex(call)       # addiu v1,zero,0x100 in the branch delay slot
    readers = scan.ld64_bit_readers(va, words, 0x1A0, 8)
    assert sorted(a for a, _, _ in readers) == [0x26642C, 0x266B0C], [hex(a) for a, _, _ in readers]
    assert {scan.function_start(words, va, (a - va) // 4) for a, _, _ in readers} == {THREAT_FN}
    print('1431: setter 1 caller, getter 3 spawn callers, reader branches in %#x only: %s' % (THREAT_FN, [hex(a) for a, _, _ in readers]))
    flag1593 = scan.ld64_bit_readers(va, words, 0x1A0, 34)
    assert [hex(a) for a, _, _ in flag1593] == ['0x31ce38'], [hex(a) for a, _, _ in flag1593]
    assert {scan.function_start(words, va, (a - va) // 4) for a, _, _ in flag1593} == {0x31CC18}
    # the reader sits under `if (wanted->m_CurrentCops (+0x808) != 0)` and on a clear flag calls 0x3081A0(ped, 3)
    assert at(0x31CE2C) >> 26 == 0x24 and (at(0x31CE2C) & 0xFFFF) == 0x808          # lbu v1,0x808(v0)
    assert at(0x31CE58) & 0xFFFF == 3 and (at(0x31CE54) >> 26) == 3                  # jal 0x3081a0 ; addiu a1,zero,3
    _, _, uses = scan.byte_mode(va, words, 0x1A4, 0x04, False)
    narrow = [u for u in uses if (at(u[0]) >> 26) != 0x37 and u[1] != 0x220198]
    assert not narrow, narrow                         # no narrow-load bit test; 0x220188 is a bounds-checked (<6) jump-table index
    assert at(0x22018C) >> 26 == 0x0B and (at(0x22018C) & 0xFFFF) == 6
    print('1593: exactly one doubleword bit-34 reader (0x31CE38 in 0x31CC18): cops present and flag clear -> 0x3081A0(ped,3);')
    print('      no narrow-load reader (0x220188 is a bounds-checked jump-table index)')
    print('LIMITS: linear scan, untyped bases, flag moved through memory/vector registers not modelled; adaptation evidence only')


if __name__ == '__main__':
    main(sys.argv[1])

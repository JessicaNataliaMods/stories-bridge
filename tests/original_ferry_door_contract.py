"""Retail PS2 ferry door timing, as ELF data only (src for include/lcs/ferry_service.hpp door_ms and the pose ratio).

Usage: original_ferry_door_contract.py <SLUS_214.23>

Facts checked on the image (SLUS_214.23, executable text at 0x100000):
* CFerry::OpenDoor (0x13F310) stores state 1 (opening) at +0x368 and timer = now + 0x2710 (10000 ms) at +0x360; CFerry::CloseDoor
  (0x13F4xx) does the same with its own state value and the same 10000 ms.
* ProcessControl's door switch computes the swing ratio as (timer - now) * 0.0001f (constant 0x38D1B717 built at 0x13E784/0x13E788
  and 0x13E80C/0x13E810), so the whole 0..1 swing spans the 10 s timer. The reLCS source divides by 1000 and would swing only during
  the last second; the executable is the reference.
* OpenFerryDoor (0x13EE80) picks the node pair from the byte at this+0x378 (front/back) and applies the 0x30-byte CDoor entries
  (stride 0x30 from this+0x3A0) through CDoor::Open(ratio) 0x299DE8, i.e. angle = open_angle * ratio.
Limits: the four open angles (+90, -95, -90, +95 degrees) are taken from reLCS Ferry.cpp; the executable builds them in the
constructor without a recognisable 32-bit constant, so they are not pinned here.
"""
import struct
import sys


def main(path):
    elf = open(path, 'rb').read()
    phoff, = struct.unpack_from('<I', elf, 0x1C)
    _, off, va, _, fsz, _, _, _ = struct.unpack_from('<IIIIIIII', elf, phoff)
    words = struct.unpack_from('<%dI' % (fsz // 4), elf, off)

    def at(address):
        return words[(address - va) // 4]

    def ins(address, op, rs=None, rt=None, imm=None):
        w = at(address)
        assert (w >> 26) == op, hex(address)
        if rs is not None:
            assert ((w >> 21) & 31) == rs, hex(address)
        if rt is not None:
            assert ((w >> 16) & 31) == rt, hex(address)
        if imm is not None:
            assert (w & 0xFFFF) == imm, hex(address)

    # OpenDoor: sh state=1 -> +0x368 ; addiu v1,v1,0x2710 ; sw v1,0x360(s3)
    ins(0x13F344, 9, rt=2, imm=1)                    # addiu v0,zero,1
    ins(0x13F348, 0x29, imm=0x368)                   # sh v0,0x368(s3)  (state)
    ins(0x13F34C, 9, imm=0x2710)                     # addiu v1,v1,10000
    ins(0x13F354, 0x2B, imm=0x360)                   # sw v1,0x360(s3)  (timer)
    # CloseDoor: the same timer
    ins(0x13F4F8, 9, imm=0x2710)
    # ratio constant 0.0001f = 0x38D1B717 built twice in the door switch
    for lui, ori in ((0x13E784, 0x13E788), (0x13E80C, 0x13E810)):
        ins(lui, 0x0F, imm=0x38D1)
        assert (at(ori) >> 26) == 0x0D and (at(ori) & 0xFFFF) == 0xB717 or ((at(ori) >> 26) == 9 and (at(ori) & 0xFFFF) == 0xB717), hex(ori)
    # opening starts from 1.0f (lui 0x3F80 at 0x13E794) and subtracts; closing multiplies directly (no 1.0f at 0x13E80C path)
    ins(0x13E794, 0x0F, imm=0x3F80)
    # OpenFerryDoor: node choice from +0x378, CDoor stride 0x30 from +0x3A0, CDoor::Open at 0x299DE8
    ins(0x13EEB0, 0x23, imm=0x378)                   # lw a0,0x378(s3)
    assert (at(0x13EF24) >> 26) == 0 and (at(0x13EF24) & 0x3F) == 0x18                      # mult s2 *= 0x30 (func 0x18)
    ins(0x13EF28, 9, imm=0x3A0)
    ins(0x13EEEC, 9, rt=17, imm=0x30)                # addiu s1,zero,0x30
    assert at(0x13EF3C) == ((3 << 26) | (0x299DE8 >> 2)), 'CDoor::Open call'
    assert at(0x13EF50) == ((3 << 26) | (0x299DE8 >> 2)), 'CDoor::Open call (second leaf)'
    print('PASS: retail ferry doors: 10000 ms timers, swing ratio (timer-now)*0.0001, nodes by +0x378, CDoor stride 0x30 at +0x3A0')


if __name__ == '__main__':
    main(*sys.argv[1:])

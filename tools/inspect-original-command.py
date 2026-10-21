"""Inspect the original PS2 command dispatcher as ELF data, without execution.

Supports the reviewed SLUS_214.23 image only. Neither a table entry nor a
disassembled handler grants runtime support in the SA bridge. Game binaries
are supplied locally by the user; this tool contains no game assets.
Capstone is optional, and is used only when displaying instructions.
"""
import argparse
import dataclasses
import hashlib
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILE_SHA256 = '65bc2ec3d8a1b9f8482fb7e69427f2539e7e7b49d9dd262bf31aa8aa54ec53b2'
# The retail outer dispatcher has several boundaries shifted by five; do not
# infer a group from opcode // 100 or the reverse's method names.
GROUPS = (
    (100, 0x10A9A0), (200, 0x10DA48), (305, 0x10FF30),
    (405, 0x208400), (505, 0x20A4A0), (605, 0x25B338),
    (705, 0x25D490), (805, 0x25EB18), (905, 0x225570),
    (1005, 0x228618), (1105, 0x267490), (1205, 0x269200),
    (1305, 0x185218), (1405, 0x187BD0), (1497, 0x24DD28),
    (1600, 0x2F0DB0), (1700, 0x273080),
)


@dataclasses.dataclass(frozen=True)
class Section:
    kind: int
    flags: int
    address: int
    offset: int
    size: int


class OriginalImage:
    def __init__(self, path):
        self.data = path.read_bytes()
        if len(self.data) < 52 or self.data[:7] != b'\x7fELF\x01\x01\x01':
            raise ValueError('Expected a little-endian ELF32 image')
        h = struct.unpack_from('<HHIIIIIHHHHHH', self.data, 16)
        if h[0:3] != (2, 8, 1) or h[7] != 52 or h[10] != 40:
            raise ValueError('Unsupported ELF header/machine')
        if not h[11] or h[11] > 4096 or h[5] > len(self.data) - h[11] * 40:
            raise ValueError('ELF section table is outside the file')
        self.sections = []
        for i in range(h[11]):
            s = struct.unpack_from('<IIIIIIIIII', self.data, h[5] + i * 40)
            section = Section(s[1], s[2], s[3], s[4], s[5])
            if section.address + section.size > 0x100000000:
                raise ValueError('ELF address range overflows')
            if section.kind != 8 and (section.offset > len(self.data) or
                                      section.size > len(self.data) - section.offset):
                raise ValueError('ELF section data is outside the file')
            self.sections.append(section)
        backed = sorted((s.address, s.address + s.size) for s in self.sections
                        if s.flags & 2 and s.kind != 8 and s.size)
        if any(b > c for (_, b), (c, _) in zip(backed, backed[1:])):
            raise ValueError('Ambiguous overlapping ELF address ranges')
        self.sha256 = hashlib.sha256(self.data).hexdigest()

    def read(self, address, size):
        if address < 0 or size <= 0 or address + size > 0x100000000:
            raise ValueError('Invalid ELF read range')
        for s in self.sections:
            if s.flags & 2 and s.kind != 8 and s.address <= address and address + size <= s.address + s.size:
                offset = s.offset + address - s.address
                return self.data[offset:offset + size]
        raise ValueError(f'Address 0x{address:X} is not backed by allocated ELF data')

    def word(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def require_profile(self):
        if self.sha256 != PROFILE_SHA256:
            raise ValueError('Unreviewed PS2 executable hash; no dispatcher inference allowed')

    def executable(self, address):
        return address % 4 == 0 and any(s.flags & 4 and s.kind != 8 and
                                      s.address <= address < s.address + s.size for s in self.sections)

    def handler(self, opcode):
        self.require_profile()
        if not 0 <= opcode < GROUPS[-1][0]:
            raise ValueError('Opcode outside reviewed original dispatcher')
        group = next(entry for end, entry in GROUPS if opcode < end)
        # Evaluate only the reviewed integer switch prologue, as data. No
        # emulation, game loading, function calls or speculative jump targets.
        registers = {0: 0, 5: opcode}
        base, count, default, table, target = 0, None, None, None, None
        for address in range(group, group + 192, 4):
            word = self.word(address)
            op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
            rd, shift, funct = (word >> 11) & 31, (word >> 6) & 31, word & 63
            immediate = word & 0xffff
            signed = immediate if immediate < 0x8000 else immediate - 0x10000
            if op == 9 and rs in registers and rt in (2, 3, 5):  # ADDIU
                registers[rt] = (registers[rs] + signed) & 0xffffffff
                if rs == 5:
                    base = -signed
            elif op == 15:  # LUI
                registers[rt] = immediate << 16
            elif op == 11 and rs in registers:  # SLTIU bounds guard
                count = immediate
                registers[rt] = int(registers[rs] < immediate)
            elif op == 4 and rs in registers and rt == 0 and count is not None:
                default = address + 4 + signed * 4
                if registers[rs] == 0:
                    if not self.executable(default):
                        raise ValueError('Default handler is outside executable ELF data')
                    return dict(opcode=opcode, group=group, handler=default, default=True,
                                table=None, base=base, count=count)
            elif op == 0 and funct == 0 and rt in registers:  # SLL
                registers[rd] = (registers[rt] << shift) & 0xffffffff
            elif op == 0 and funct == 0x21 and rs in registers and rt in registers:  # ADDU
                registers[rd] = (registers[rs] + registers[rt]) & 0xffffffff
            elif op == 35 and rs in registers and rt == 4:  # LW handler target
                pointer = registers[rs] + signed
                if count is None or not 0 <= opcode - base < count:
                    raise ValueError('Unverified switch index')
                table = pointer - (opcode - base) * 4
                target = self.word(pointer)
                registers[rt] = target
            elif op == 0 and funct == 8 and rs == 4:  # JR table target
                if target is None or not self.executable(target):
                    raise ValueError('Table handler is outside executable ELF data')
                return dict(opcode=opcode, group=group, handler=target, default=target == default,
                            table=table, base=base, count=count)
        raise ValueError('Original switch layout not recognized')

    def instructions(self, address, size):
        self.require_profile()
        if address % 4 or size <= 0 or size % 4 or size > 0x10000:
            raise ValueError('Disassembly requires bounded, aligned words')
        sys.path.insert(0, str(ROOT / 'build/analysis-deps'))
        from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS64, CS_MODE_LITTLE_ENDIAN
        disassembler = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_LITTLE_ENDIAN)
        data = self.read(address, size)
        for offset in range(0, size, 4):
            raw = data[offset:offset + 4]
            instruction = next(disassembler.disasm(raw, address + offset), None)
            # PS2 EE extensions are not all decoded by Capstone. An unknown
            # word is preserved, never dropped nor claimed to be a NOP.
            yield dict(address=address + offset, word=raw.hex(),
                       assembly=f'{instruction.mnemonic} {instruction.op_str}' if instruction else
                       f'.word 0x{struct.unpack("<I", raw)[0]:08X}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=pathlib.Path)
    selector = parser.add_mutually_exclusive_group(required=True)
    selector.add_argument('--opcode', type=lambda value: int(value, 0))
    selector.add_argument('--address', type=lambda value: int(value, 0))
    parser.add_argument('--size', type=lambda value: int(value, 0), default=256)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    image = OriginalImage(args.executable)
    report = dict(gameExecuted=False, runtimeSupportGranted=False, executableSha256=image.sha256)
    if args.opcode is not None:
        report['dispatch'] = image.handler(args.opcode)
        address = report['dispatch']['handler']
    else:
        address = args.address
    report['instructions'] = list(image.instructions(address, args.size))
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps({k: v for k, v in report.items() if k != 'instructions'}))
        for instruction in report['instructions']:
            print(f'{instruction["address"]:08X} {instruction["word"]} {instruction["assembly"]}')

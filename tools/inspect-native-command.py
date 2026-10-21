"""Read native SA dispatcher/handlers as PE data; never start the game.

Requires Capstone (local analysis dependency), installed under build/analysis-deps.
Only the verified US dispatcher layout is supported. This tool is diagnostic,
not a decoder that grants runtime command support.
"""
import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'build/analysis-deps'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_EAX, X86_OP_IMM, X86_REG_EBP, X86_REG_EDI, X86_REG_ECX, X86_REG_EBX, X86_REG_ESP


class NativeImage:
    def __init__(self, path):
        self.data = path.read_bytes()
        if len(self.data) < 64:
            raise ValueError('Truncated native DOS header')
        nt = struct.unpack_from('<I', self.data, 60)[0]
        if nt+88 > len(self.data) or self.data[:2] != b'MZ' or self.data[nt:nt+4] != b'PE\0\0' or struct.unpack_from('<I', self.data, nt+52)[0] != 0x400000 or struct.unpack_from('<H',self.data,nt+4)[0] != 0x14c or struct.unpack_from('<H',self.data,nt+24)[0] != 0x10b:
            raise ValueError('Unsupported native PE profile')
        sections = nt+24+struct.unpack_from('<H', self.data, nt+20)[0]
        count = struct.unpack_from('<H', self.data, nt+6)[0]
        if not count or sections+count*40 > len(self.data):
            raise ValueError('Truncated native section table')
        self.spans = [struct.unpack_from('<IIII', self.data, sections+i*40+8)
                      for i in range(count)]
        if any(raw+size > len(self.data) or start+size > 0xffc00000 for _,start,size,raw in self.spans):
            raise ValueError('Native section exceeds file/address bounds')
        self.executable_spans = [(start, size) for i, (_, start, size, _) in enumerate(self.spans)
                                 if struct.unpack_from('<I', self.data, sections+i*40+36)[0] & 0x20000000]
        self.disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
        self.disassembler.detail = True

    def read(self, address, length):
        if length <= 0 or address < 0x400000 or address+length > 0x100000000:
            raise ValueError('Invalid native address/length')
        rva = address-0x400000
        for _, start, size, raw in self.spans:
            if start <= rva and rva+length <= start+size:
                return self.data[raw+rva-start:raw+rva-start+length]
        raise ValueError('Address is outside the on-disk image')

    def executable(self, address):
        return any(start <= address-0x400000 < start+size for start, size in self.executable_spans)

    def word(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def instructions(self, address, length):
        return self.disassembler.disasm(self.read(address, length), address)

    def handler(self, opcode):
        if not 0 <= opcode < 2700:
            raise ValueError('Opcode outside reviewed dispatcher')
        group = self.word(0x8A6168+(opcode//100)*4)
        registers = {}
        bounds = {}
        for instruction in self.instructions(group, 160):
            operands = instruction.operands
            if len(operands) == 2 and operands[0].type == X86_OP_REG:
                dest, source = operands[0].reg, operands[1]
                if instruction.mnemonic == 'mov' and source.type == X86_OP_MEM and source.mem.base == X86_REG_ESP and not source.mem.index:
                    registers[dest] = opcode
                if instruction.mnemonic in ('add', 'sub') and dest in registers and source.type == X86_OP_IMM:
                    registers[dest] = (registers[dest] + (source.imm if instruction.mnemonic == 'add' else -source.imm)) & 0xffffffff
                if instruction.mnemonic == 'lea' and source.type == X86_OP_MEM and source.mem.base in registers and not source.mem.index:
                    registers[dest] = (registers[source.mem.base] + source.mem.disp) & 0xffffffff
                if instruction.mnemonic == 'cmp' and dest in registers and source.type == X86_OP_IMM:
                    bounds[dest] = (registers[dest], source.imm & 0xffffffff)
                if instruction.mnemonic == 'movzx' and source.type == X86_OP_MEM and source.mem.base in registers and not source.mem.index:
                    if source.size != 1:
                        raise ValueError('Unverified native compact table width')
                    registers[dest] = self.read(source.mem.disp+registers[source.mem.base], 1)[0]
            if instruction.mnemonic == 'ja' and len(operands) == 1 and operands[0].type == X86_OP_IMM:
                if len(bounds) != 1:
                    raise ValueError('Unverified native dispatcher bound')
                index, limit = next(iter(bounds.values()))
                if index > limit:
                    target = operands[0].imm
                    if not self.executable(target):
                        raise ValueError('Native default target is not executable')
                    return group, target
            if instruction.mnemonic == 'jmp' and len(operands) == 1 and operands[0].type == X86_OP_MEM and operands[0].mem.scale == 4:
                reg = operands[0].mem.index
                if reg not in registers or not bounds or not 0 <= registers[reg] < 256:
                    raise ValueError('Unverified native jump index')
                target = self.word(operands[0].mem.disp+4*registers[reg])
                if not self.executable(target):
                    raise ValueError('Native jump target is not executable')
                return group, target
        raise ValueError('Native switch layout not recognized')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=pathlib.Path)
    selector = parser.add_mutually_exclusive_group(required=True)
    selector.add_argument('--opcode', type=lambda value: int(value, 0))
    selector.add_argument('--address', type=lambda value: int(value, 0))
    parser.add_argument('--size', type=lambda value: int(value, 0), default=256)
    args = parser.parse_args()
    image = NativeImage(args.executable)
    if args.opcode is not None:
        group, address = image.handler(args.opcode)
        print(f'opcode=0x{args.opcode:04X} dispatcher=0x{group:08X} handler=0x{address:08X}; gameExecuted=false')
    else:
        address = args.address
    for instruction in image.instructions(address, args.size):
        print(f'{instruction.address:08X} {instruction.mnemonic} {instruction.op_str}')

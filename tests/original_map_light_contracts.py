"""Read the user's PE as data to verify the native map-light call boundary."""
import importlib.util
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('map_light_native', ROOT/'tools/inspect-native-command.py')
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
if len(sys.argv) != 2:
    raise SystemExit('Pass the native SA PE path; it is never executed')
pe = native.NativeImage(Path(sys.argv[1]))
assert pe.read(0x535FBE, 15).hex() == '8bd88a470d84c0895c242074078bce'
call = pe.read(0x535FCD, 5)
assert call[0] == 0xE8
assert 0x535FD2+int.from_bytes(call[1:], 'little', signed=True) == 0x6FC7A0
assert pe.read(0x535FD2, 2) == b'\x33\xc0'
calls = {instruction.operands[0].imm for instruction in pe.instructions(0x49DAB0, 0x700)
         if instruction.mnemonic == 'call' and instruction.operands[0].type == native.X86_OP_IMM}
assert {0x49D520, 0x49D4D0, 0x49D350, 0x49D3A0}.issubset(calls)
direction_calls = {i.operands[0].imm for i in pe.instructions(0x49D580, 32)
                   if i.mnemonic == 'call' and i.operands[0].type == native.X86_OP_IMM}
assert 0x49D520 in direction_calls
display = list(pe.instructions(0x49DAB0, 0x240))
# Match the native front/back filter's camera-forward storage and matrix axis.
assert any('0xb6f9ac' in i.op_str for i in display)
assert any('0xb6f9b0' in i.op_str for i in display)
assert any('0xb6f9b4' in i.op_str for i in display)
assert any(i.mnemonic == 'call' and i.op_str == '0x4c4c70' for i in display)
print('PASS: native visible-entity thiscall boundary and orientation/visual signal phases; game not executed')

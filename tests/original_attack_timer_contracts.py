"""Static ELF+PE evidence for LCS 1586, no executable launch or ABI claim."""
import importlib.util
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec)
    sys.modules[name]=module
    spec.loader.exec_module(module)
    return module

elf_api=load('source_attack_timer_1586',ROOT/'tools/inspect-original-command.py')
elf=elf_api.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()
contract=elf.handler(1586)
assert contract['handler']==0x2F3364 and not contract['default'],contract
# Inputs: original ped handle (0), full raw 32-bit timer (1).
assert elf.word(0x2F3380)==0x8E05F320  # actually verify literal below against retail
assert elf.word(0x2F3394)==0xAC4304C8  # sw v1,0x4c8(v0)
# 1585 is a DIFFERENT original field and not silently covered by 1586.
other=elf.handler(1585)
assert other['handler']==0x2F3330 and elf.word(0x2F3360)==0xAC4304C4

pe_api=load('native_attack_timer_1586',ROOT/'tools/inspect-native-command.py')
pe=pe_api.NativeImage(Path(sys.argv[1]))
for addr in (0x5DDD01,0x5E81B4):
    assert pe.read(addr,6)==bytes.fromhex('89 9E 4C 07 00 00'),hex(addr)
# These independently documented PE sites initialize SA CPed+0x74C.
print('PASS: 1586 writes raw LCS ped timer +0x4C8; SA initializes own attack timer +0x74C; no Win32 runtime proof')

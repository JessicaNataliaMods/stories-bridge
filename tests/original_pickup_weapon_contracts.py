"""Validate opcode 1487 native signatures on supplied ELF/PE as read-only data.

No game code, PE, ASI, Wine or executable is launched. This is a static
contract check, NOT a replacement for the unrun Win32 native backend test.
"""
import importlib.util
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]

def load(module,path):
    spec=importlib.util.spec_from_file_location(module,path)
    result=importlib.util.module_from_spec(spec)
    sys.modules[module]=result
    spec.loader.exec_module(result)
    return result

source=load('source_pickup_1487',ROOT/'tools/inspect-original-command.py')
elf=source.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()
handler=elf.handler(1487)
assert handler['handler']==0x250084 and not handler['default'],handler
assert elf.word(0x25009C)==0x24060004  # CollectParameters(..., 4)
assert elf.word(0x2500A0)==0x0C042532  # collects 4 actual parameters
assert elf.word(0x2500A8)==0x2412FFFF  # no pickup: model = -1
assert elf.word(0x2500F0)==0x92220032  # check original pickup type
assert elf.word(0x250170)==0x8E220014  # original pickup owns object pointer
assert elf.word(0x25017C)==0x84440058  # fetch actual object's model
assert elf.word(0x250180)==0x0C071D1A  # WeaponForModel call
assert elf.word(0x250184)==0x24100001  # found=true for matching slot
assert elf.word(0x250188)==0x0040902D  # overwrite last matched type
assert elf.word(0x25015C)==0x46000004  # sqrt.s f0,f0, not squared-radius double math
assert elf.word(0x250160)==0x46140034  # c.olt.s f0,f20, strict original float boundary
native=load('native_pickup_1487',ROOT/'tools/inspect-native-command.py')
pe=native.NativeImage(Path(sys.argv[1]))
assert pe.read(0x4552A0,5).hex()=='e90bf91001'  # verified pickup handle entry
assert pe.read(0x1564BB0,37).hex()=='8b4c240483f9ff74188bc125ffff00008bd0c1e205c1e910663b8ada889700740383c8ffc3'
print('PASS: LCS 1487 four-operand scan, live object, last-win type and SA pickup ABI static signatures; gameExecuted=false')

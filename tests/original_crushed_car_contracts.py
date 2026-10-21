"""Data-only verification: retail LCS opcode 0334 and SA garage crusher field.

This probes static executable bytes; it cannot replace Win32 compilation/game tests.
"""
import importlib.util
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec)
    sys.modules[name]=result
    spec.loader.exec_module(result)
    return result

lcs=module('lcs_crushed_car_334',ROOT/'tools/inspect-original-command.py')
elf=lcs.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()
handler=elf.handler(334)
assert not handler['default'] and handler['handler']==0x2085FC,handler
assert elf.word(0x208608)==0x24060001  # exactly one script argument
assert elf.word(0x208614)==0x0C066B84  # actual HasCarBeenCrushed call
# HasCarBeenCrushed reads full 32-bit LCS handle, XOR, and boolean equality.
assert [elf.word(a) for a in (0x19AE10,0x19AE14,0x19AE18,0x19AE1C,0x19AE20)]==[
    0x3C02003E,0x8C429D30,0x00441026,0x03E00008,0x2C420001]
sa=module('sa_crushed_car_334',ROOT/'tools/inspect-native-command.py')
pe=sa.NativeImage(Path(sys.argv[1]))
assert pe.read(0x447120,6).hex()=='5333db83c8ff'  # SA CGarages::Init sets EAX=-1
assert pe.read(0x44716E,5).hex()=='a3e8bf9600'  # last-crushed handle field
print('PASS: LCS 0334 full-handle equality and SA CGarages::CrushedCarId 0x96BFE8; static only')

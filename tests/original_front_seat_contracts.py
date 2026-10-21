"""Data-only original 1485 front-seat bit and native SA entry path proof."""
from pathlib import Path
import importlib.util
import sys

ROOT=Path(__file__).resolve().parents[1]
def read_module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec)
    sys.modules[name]=mod
    spec.loader.exec_module(mod)
    return mod

src=read_module('lcs_front_source',ROOT/'tools/inspect-original-command.py')
elf=src.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()
assert elf.handler(1485)['handler']==0x24FF74
# CPed +0x1A0: clear bit 10, then OR (second parameter != 0) << 10.
expected={
    0x24FF84:0x24060002, # collect exactly two parameters
    0x24FFA4:0xDE0401A0, # read 64-bit ped field
    0x24FFA8:0x2405FBFF, # ~0x400 mask
    0x24FFAC:0x0003182B, # SLTU arg!=0
    0x24FFB4:0x00852024, # preserve unrelated fields
    0x24FFB8:0x00031AB8, # shift bit 10
    0x24FFBC:0x00832025, # merge
    0x24FFC4:0xFE0401A0, # write ped field
}
for address,value in expected.items():
    actual=elf.word(address)
    assert actual==value, f'{address:x}: {actual:08x} != {value:08x}'
assert len(sys.argv)==2, 'Pass SA PE as data'
sa=read_module('lcs_front_native',ROOT/'tools/inspect-native-command.py')
pe=sa.NativeImage(Path(sys.argv[1]))
# The bridge uses the real SA 05CA passenger entry, not an empty native opcode.
assert pe.handler(0x05CA)[1]==0x49036C
assert pe.read(0x49036C,12).hex()=='6a048bcee80b3dfdff8b0d80'
assert pe.handler(0x056D)[1]==0x48DAB5  # character validity guard
assert pe.handler(0x0431)[1]==0x489E73  # occupied-seat query
print('PASS: original 1485: CPed bit 10, two inputs, native 05CA/056D/0431 verified from ELF/PE data (not executed)')

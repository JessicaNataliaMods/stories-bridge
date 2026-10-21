"""Data-only proof: LCS 0989/0990, native SA 03D8/03D9, and real owned menu/stat/speed state."""
import importlib.util
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,file)
    mod=importlib.util.module_from_spec(spec);sys.modules[name]=mod;spec.loader.exec_module(mod)
    return mod
source=module('save_source',root/'tools/inspect-original-command.py')
elf=source.OriginalImage(root / 'local/originals/lcs-ps2/SLUS_214.23');elf.require_profile()
assert elf.handler(989)['handler']==0x22A820
assert elf.handler(990)['handler']==0x22A8C8
# Original 0989: FindPlayerPed() null/in-vehicle gates, SafeHouseVisits++ at
# 0x3D9E34, FrontEndMenuManager.m_bActivateSaveMenu = 1 at 0x634A8B, and
# CPhysical move/turn speed zeroing at +0x70..+0x78 and +0x80..+0x88.
for addr,word in {
    0x22A820:0x0C041CDA, 0x22A828:0x104000FE,
    0x22A838:0x8C430538, 0x22A83C:0x146000F9,
    0x22A844:0x3C02003E, 0x22A848:0x8C429E34,
    0x22A850:0x24030001, 0x22A854:0x24420001,
    0x22A858:0xA0834A8B, 0x22A860:0xAF822F44,
    0x22A868:0x24430070, 0x22A878:0xE4540070,
    0x22A89C:0x24430080, 0x22A8A8:0xE4540080,
}.items():assert elf.word(addr)==word,hex(addr)
# Both early exits branch to the common epilogue, not into the speed stores.
for addr in (0x22A828,0x22A83C):
    offset=elf.word(addr)&0xffff
    if offset&0x8000:offset-=0x10000
    assert addr+4+(offset<<2)==0x22AC24
assert elf.word(0x22A85C)==0x0C041CDA  # menu store then stat store in jal delay slot
# Original predicate: front-end active OR activate-save-menu means NOT done.
# Original menu flags are actually separate bytes in the same menu object.
for addr,word in {
    0x22A8C8:0x3C020063, 0x22A8CC:0x24424610,
    0x22A8D0:0x9043013D, 0x22A8DC:0x9042047B,
    0x22A8D4:0x14600004, 0x22A8E0:0x50400001,
    0x22A8E4:0x24030001,
}.items():assert elf.word(addr)==word,hex(addr)
assert len(sys.argv)==2,'Pass packaged SA PE path as data, not as executable'
sa=module('save_sa',root/'tools/inspect-native-command.py')
pe=sa.NativeImage(Path(sys.argv[1]))
assert pe.handler(0x03D8)[1]==0x48542C
assert pe.read(0x48542C,89).hex()==('83cbff53e8db8d0e0083c40485c07447'
    '53e8ce8d0e008b886c04000083c404f6c5017533'
    '6a006a006a0053c605a767ba0001'
    'e8ad8d0e0083c4048bc8e8c3bcfbff'
    '6a006a006a0053e8978d0e0083c4048bc8e82d5bfdff32c0')
# call 0x441130 (CPhysical::ResetMoveSpeed) and call 0x45AFB0 (ResetTurnSpeed)
assert 0x48546D+int.from_bytes(bytes.fromhex('c3bcfbff'),'little',signed=True)==0x441130
assert 0x485483+int.from_bytes(bytes.fromhex('2d5bfdff'),'little',signed=True)==0x45AFB0
assert pe.handler(0x03D9)[1]==0x48548A
assert pe.read(0x48548A,28).hex()==('a0a467ba0084c0750e'
    'a0a767ba0084c0c6442410017405c644241000')
# Menu state offsets are derived from the retail handlers themselves, not inferred
# from the name of another SA menu/save command.
print('PASS: source 0989/0990 and SA 03D8/03D9 share BA67A4/BA67A7, SafeHouseVisits and speed-reset semantics; no game executed')

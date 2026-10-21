"""LCS critical-failure order and the real SA restart/failure consumers; data only."""
import importlib.util
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);sys.modules[name]=result;spec.loader.exec_module(result)
    return result

original=module('critical_original',ROOT/'tools/inspect-original-command.py')
elf=original.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23');elf.require_profile()
assert elf.handler(602)['handler']==0x25D2D4
for address,value in {
    0x25D2E4:0x24060004,  # four source operands
    0x25D2F4:0x3C01C2C8,  # -100 ground sentinel
    0x25D300:0x4600A836,  # Z <= sentinel
    0x25D314:0x0C05E53E,  # real ground query
    0x25D330:0x0C0B69CC,  # OverrideNextRestart, before player failure
    0x25D368:0x0C041DD0,  # PlayerFailedCriticalMission
}.items():assert elf.word(address)==value,hex(address)

native=module('critical_native',ROOT/'tools/inspect-native-command.py')
assert len(sys.argv)==2, 'Pass SA PE as data'
pe=native.NativeImage(Path(sys.argv[1]))
assert pe.handler(0x255)==(0x47E090,0x47F235)  # named opcode is unsupported retail default
assert pe.read(0x47C95E,9).hex()=='6a048bcee81977feff'
call=pe.read(0x47C9CD,5)
assert call[0]==0xE8 and 0x47C9D2+int.from_bytes(call[1:],'little',signed=True)==0x4607D0
assert pe.read(0x56E830,39).hex()=='8a81dc00000084c0751cb0038881dc000000a2b0a89600a184cbb700a3aca89600e9baf3ecffc3'
print('PASS: LCS602 ground/override/failure order; SA0255 unsupported; real CPlayerInfo failure state, game-logic timer and Darkel reset consumer; no game executed')

"""Independent ELF/PE script corona contracts; no game execution."""
from pathlib import Path
import importlib.util
import sys
ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);sys.modules[name]=m;s.loader.exec_module(m);return m
src=module('corona_original',ROOT/'tools/inspect-original-command.py')
elf=src.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23');elf.require_profile()
assert elf.handler(1481)['handler']==0x24FCB8
assert elf.handler(1482)['handler']==0x24FD8C
words={
0x24FCCC:0x24060009,0x24FCDC:0x24040028,
0x24FCEC:0x8C429C50,0x24FD00:0xAE020008,0x24FD04:0x24420001,
0x24FD10:0xAC229C50,0x24FCF8:0x3C01C2C8,0x24FD08:0x46000836,
0x24FD24:0x0C05E53E,0x24FD2C:0xE6000014,
0x24FD38:0x92220018,0x24FD48:0x9223001C,0x24FD4C:0x92270020,
0x24FD60:0xA202001C,0x24FD68:0xA203001D,0x24FD6C:0xA207001E,
0x24FD70:0xAE080020,0x24FD74:0xAE0B0024,0x24FD80:0xAC309424,
0x24FD88:0x24060001, # actual one output, absent in ScriptDebug
0x24FD98:0x24060001,0x24FDC4:0x8E020008,0x24FDC8:0x5443000B,
0x24FDD8:0x0C09418C,
# Real per-frame emitter: pointer identity, XYZ, negative size, RGBA,
# far450, near1.5, actual RegisterCorona259710 call and reflection1.
0x10A56C:0x240800FF,0x10A570:0x3C0143E1,0x10A580:0x0040202D,
0x10A59C:0xC44C0018,0x10A5A4:0x46006307,
0x10A5B0:0x904A0020,0x10A5B4:0x904B0024,
0x10A5B8:0x3C013FC0,0x10A5C0:0xAFB10000,0x10A5CC:0x0C0965C4,
}
for address,expected in words.items():assert elf.word(address)==expected,hex(address)
assert len(sys.argv)==2,'Pass native SA image as PE data'
sa=module('corona_native',ROOT/'tools/inspect-native-command.py');pe=sa.NativeImage(Path(sys.argv[1]))
assert pe.read(0x6FC180,12).hex()=='83ec1c55568b74242c85f657'
assert pe.read(0x6FC580,12).hex()=='0fb65424288b049500e0c300'
assert pe.read(0x569660,11).hex()=='83ec388b44243c8b4c2440'
ground_call=pe.read(0x47F0CB,5)
assert ground_call[0]==0xE8 and 0x47F0D0+int.from_bytes(ground_call[1:],'little',signed=True)==0x569660
assert pe.handler(0x24F)[1]==0x47F08E
assert pe.read(0x47F0F3,5).hex()=='6800007041' # fadeSpeed15
assert pe.read(0x47F0FA,5).hex()=='680000c03f' # near1.5
assert pe.read(0x47F132,5).hex()=='680000e143' # far450
call=pe.read(0x47F18E,5)
assert call[0]==0xE8 and 0x47F193+int.from_bytes(call[1:],'little',signed=True)==0x6FC580
print('PASS: retail1481/1482 input/output/list/frame contracts and native corona ABI/defaults; no game/rendering executed')

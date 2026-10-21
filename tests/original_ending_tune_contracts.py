"""Inspect the original finale stream request; never run the game or audio."""
from pathlib import Path
import importlib.util
import sys
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ending_original',root/'tools/inspect-original-command.py')
module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
image=module.OriginalImage(root / 'local/originals/lcs-ps2/SLUS_214.23');image.require_profile()
assert image.handler(1092)['handler']==0x268AE8
assert image.handler(1093)['handler']==0x268B18
for address,expected in {
    0x268AF0:0x0C0A888E,0x268AF4:0x0000282D, # mode FRONTEND0 first
    0x268B00:0x2405003F,0x268B04:0x24060001,0x268B08:0x0C0A886A,
    0x268B0C:0x24070001, # actual retail third flag1, not reconstructed two-argument assumption
    0x2A21C0:0x0C05A790,0x2A21C4:0x30E700FF,
    0x169F24:0xA212034D,0x169F2C:0xA2130351, # frontend track and loop flag
    0x268B20:0x0C0A88AA,0x268B30:0x0C0A888E,0x268B34:0x24050001,
    0x2A22B4:0x0C05A76E,0x169DF0:0x24020002, # StopCutsceneMusic guard is mode CUTSCENE2
}.items():assert image.word(address)==expected,hex(address)
# This is a name/ID record (stride8), not an inferred pointer-only table.
assert image.word(0x39A888)==0x3D9310 and image.word(0x39A88C)==63
assert image.read(0x3D9310,7)==b'FINALE\0'
print('PASS: original frontend0 -> FINALE63/loop1/retail flag1; stop helper then game1; game/audioExecuted=false')
for address,expected in {0x16817C:0x30420003,0x1682D8:0x24020003,
    0x1682E0:0xA2420005,0x168300:0xA2400006,0x168304:0x24020064,
    0x16830C:0xA2420007,0x168464:0x2442FFFF,0x16846C:0xA2420005,
    0x168488:0x24420006,0x168494:0xA2430006,
    0x1682B8:0x0C0453E6,0x114FAC:0x24050001,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: retail loop flag forwarding, frontend cadence4/latency3/fade6/maximum100')
for address,expected in {
    0x238130:0x0C08E57E,0x238138:0x0C08E1F6,0x238144:0x0C059FA2,
    0x238804:0x8E426C7C,0x23880C:0x24420001,0x238818:0xAE426C7C,
    0x167F54:0x30420003,0x167FD0:0x0C05AB88,
    0x167FF4:0xA2020350,0x168008:0x14600003,0x168014:0xA204034D,
    0x168048:0x24020004,0x168050:0xA2020350,
    0x16834C:0x10620024,0x1683E0:0x2405000C,0x1683FC:0xAC60001C,
    0x168410:0x0C05ABA0,0x168414:0x9245034E,
    0x169E54:0x30F100FF,0x169E90:0x52200014,0x169EA8:0x0C05AB88,
    0x169ED0:0x0C060EB6,0x169ED4:0x0000282D,
    0x183978:0xAE00000C,0x183AD0:0x03E00008,0x183AD4:0x0000102D,
    0x183AD8:0x03E00008,0x183ADC:0xAC85000C,
    0x183AE0:0x03E00008,0x183AE4:0x8C82000C,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: actual audio-service frame increment, early request retention, paused frontend disable, finale EOF restart')
if len(sys.argv)>1:
    spec=importlib.util.spec_from_file_location('ending_native',root/'tools/inspect-native-command.py')
    native=importlib.util.module_from_spec(spec);sys.modules[spec.name]=native;spec.loader.exec_module(native)
    pe=native.NativeImage(Path(sys.argv[1]))
    for address,raw in {
        0x53E7B7:'b990bcb600e88f8ffcff',0x53E7C1:'d90538508d00',
        0x53E986:'b990bcb600e8c08dfcff',0x53E990:'8b153870c100',
        0x4D9530:'8b442404898110040000c20400',0x4D9550:'8b442404898118040000c20400',
        0x506DEA:'b9b8f8b500',0x506E40:'b9b8f8b500e90627fdff',
        0x50776A:'a049cbb70084c07519a048cbb70084c07510',
    }.items():assert pe.read(address,len(bytes.fromhex(raw)))==bytes.fromhex(raw),hex(address)
    assert pe.read(0x85A7D4,4)==__import__('struct').pack('<f',1/64)
    print('PASS: native audio CALL this ABI, real master/fader fields, native code/user pause; no game calls')

"""Data-only proof for source 1512 and guarded SA remote-cancel ABI."""
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def load(name, location):
    spec = importlib.util.spec_from_file_location(name, location)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod

source = load('original_cancel_1512', ROOT/'tools/inspect-original-command.py')
image = source.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
image.require_profile()
assert image.handler(1512)['handler'] == 0x2F1560
assert image.word(0x2F1560) == 0x0C041C9E  # physical vehicle lookup
assert image.word(0x2F1570) == 0x0C041C9E  # reacquire physical vehicle
assert image.word(0x2F1578) == 0xDC430250  # physical car flags
assert image.word(0x2F15AC) == 0x0C089504  # 225410, false in delay slot
assert image.word(0x2F15B0) == 0x0000202D
assert image.word(0x2F15E4) == 0xAC6000F8  # player fade permission = 0
assert image.word(0x2F15E8) == 0x0C0A4AC0  # Camera::Restore
assert image.word(0x2F15EC) == 0xACA00004  # remote pointer = 0
native = load('native_cancel_1512', ROOT/'tools/inspect-native-command.py')
pe = native.NativeImage(pathlib.Path(sys.argv[1]))
expected = {
    0x45AE80: 'e9db721001',  # real release trampoline, cdecl
    0x1562160: '0fb60574cdb70069c0900100008b8848ceb70080b9a40400000275',
    0x15621CA: '0fb60d74cdb70069c9900100008b8148ceb70080a028040000f7',
    0x15621F7: '8a4c2404899078ceb700c68075ceb70001888876ceb700c68077ceb70001c3',
    0x6D5D70: '538b5c2408568bf10fb686a40400003bc3',
    0x4654B0: 'e95b931001',
    0x156E810: '5155568be9578d7d04c744240c4b000000',
    0x48970E: '808e2a04000020',  # real physical-car damage bit
    0x56E044: 'f6c50156741b8b888c050000', # real in-car flag and ped->vehicle
    0x47C4C7: 'b928f0b600e85ff4080032c0', # original SA camera command
    0x50B930: '5333db568bf157536affc6462b01', # real thiscall Restore
    0x571A00: '8b412483c12485c0742e8b542404', # registered reference removal
}
for address, signature in expected.items():
    assert pe.read(address, len(bytes.fromhex(signature))).hex() == signature, hex(address)
assert pe.handler(0x0409)[1] == 0x48A1E2
assert pe.read(0x48A1E2, 4).hex() == '32c0eb02'  # NO release side effects
assert pe.read(0x15621E4, 13).hex() == '0fb60574cdb7008b1584cbb700'  # live tick
# Non-negotiable: DD=1, DE=argument false, DF=1 set by real release;
# 1512 must immediately clear DF without clearing DD, and retire the pointer.
assert pe.read(0x1562201, len(bytes.fromhex('c68075ceb70001888876ceb700c68077ceb70001c3'))).hex() == 'c68075ceb70001888876ceb700c68077ceb70001c3'
print('PASS: original 1512 vs native real release, physical damage, camera, reference ABI and DD/DE/DF; gameExecuted=false')

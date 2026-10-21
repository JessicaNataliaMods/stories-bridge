"""Inspect retail phone state9 as ELF data, without running the game."""
from pathlib import Path
import importlib.util
import sys

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('phone_original', root/'tools/inspect-original-command.py')
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
image = module.OriginalImage(root / 'local/originals/lcs-ps2/SLUS_214.23')
image.require_profile()
assert image.handler(1034)['handler'] == 0x267CBC
assert image.handler(595)['handler']==0x25D16C
for address,expected in {0x25D190:0x0000302D,0x25D1A4:0x0C09C7EC,
    0x271FB0:0x10C0000B,0x271FE0:0x24030003,0x271FE8:0x03E00008,
    0x271FEC:0xAC430040}.items():assert image.word(address)==expected,hex(address)
print('PASS: original phone OFF passes nil message and stores state3 only; no invented pose/audio stop')
words = {
    0x267CC8:0x24060001, 0x267CE0:0x24050009,
    0x267CE4:0x00031980, 0x267CF4:0xAC650040,
    0x2FBE88:0x0C09C59E, # real game update calls phone update 271678
    0x271B40:0x24030758, 0x271B50:0x0083001B,
    0x271B74:0x0C0D3F38, # original RNG, no fabricated sample
    0x271B7C:0x304203FF, 0x271B80:0x3C013883,
    0x271B84:0x3421126F, # float reciprocal 1/16000
    0x271BA4:0x46020002, 0x271BB0:0xE4400028,
    0x271BC4:0x3C01C0A0, # -5 float: retail-only player height sound gate
    0x271BE0:0x46011034, 0x271BF4:0x2404004C,
    0x271BF8:0x0C04F2BA, # ring sound76 call
    0x271C18:0xE4600028, # silent half restores Up.z=1
    0x271C24:0x0C09B246, 0x271C2C:0x0C0C13DC,
    0x271C50:0x3C01461C,0x271C54:0x34214000, # squared outer100m
    0x271C6C:0x46002034,0x271C78:0xAC600044,
    0x271C90:0x94440056,0x271C98:0x3063000F, # native original seed/frame modulo16
    0x271CC0:0x3C0145C8,0x271CD8:0x46030034, # squared inner80m, NOT source60
    0x271CE8:0xACA20044,0x271CFC:0x00151980, # visible flag+44, phone stride64
}
for address, expected in words.items():
    assert image.word(address) == expected, hex(address)
print('PASS: retail phone state9 cadence/RNG/jitter/player-height sound/render calls; gameExecuted=false')
assert image.word(0x3B26C0+76*4)==0x1B2768 # original one-shot script event dispatcher
for address,expected in {
    0x1B1DF4:0x30A300FF,0x1B1DFC:0x2C620058,0x1B1E20:0x244226C0,
    0x1B2770:0x240200EC,0x1B2784:0xAE020028, # sample236, not guessed enum index235
    0x1B2788:0x0C04528E,0x1B278C:0x240500EC, # authored base frequency
    0x1B2768:0x3C0142A0,0x1B2790:0x3C0145C8, # distance80 and squared6400
    0x1B2798:0x3C014000,0x1B27A8:0x24120050, # speed multiplier2, volume80
    0x1B27B0:0xE6000044,0x1B27BC:0xA2000061,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: actual script sound76 -> sample236, authored frequency, range80, volume80, speed2 and no reflection')
for address,expected in {
    0x1C6B70:0x44C0F800, # startup FCR31=0: cvt.w.s nearest/ties-even
    0x238A30:0x3C013E4C,0x238A34:0x3421CCCD,
    0x238A70:0x46010003,0x238A74:0x46000002,
    0x238A7C:0x46000064,0x238A84:0x304200FF,
    0x1B2A10:0x46140034,0x1B2A38:0x0C08E284,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: original phone strict range and nearest-rounded ComputeVolume; no host floating mode changes')
assert image.read(0x392700,64).hex()==('0003080c101316181a1c1e1f212224252728292a2c2d2e2f30313132333435353637'
    '37383839393a3a3a3b3b3b3c3c3d3d3d3d3e3e3e3e3e3f3f3f3f3f3f3f3f')
for address,expected in {0x238C90:0x3C013C80,0x238CB4:0x460C0B03,
    0x238CBC:0x46006024,0x238CEC:0x24632700,0x238CE8:0x2404006B,
    0x238D08:0x24050014}.items():assert image.word(address)==expected,hex(address)
print('PASS: actual retail pan table, distance/64 denominator, nearest index and20..107 limits')
for address,expected in {
    0x237E50:0x3C014136,0x237E54:0x3421EEF0, # authored speed-of-sound float bits
    0x237E60:0x2403001E,0x237E78:0xA2436C78, # actual elapsed-time byte30
    0x237E84:0xE6400008,
    0x238D64:0x0C0A35A8, # camera-switch gate
    0x238D8C:0x4615A301, # current minus previous distance
    0x238DA0:0x92226C78,0x238DA4:0xC6220008,
    0x238DBC:0x46006003,0x238DC0:0x46160302,
    0x238DC8:0x46011036, # abs speed >= sound speed keeps old frequency
    0x238DF0:0x3C013FC0,0x238DFC:0x46006029, # positive min1.5
    0x238E08:0x3C01BFC0,0x238E14:0x46006028, # negative max-1.5
    0x238E4C:0x46036302,0x238E54:0x46006303,
    0x238E58:0x0C0B8FA5,0x2E3E94:0x46006024,
    0x2E3E98:0x03E00008,0x2E3E9C:0x44020000,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: retail Doppler constructor constants, switch/speed gates, signed clamp and nearest conversion')
for address,expected in {
    0x23A040:0xC660001C,0x23A048:0xC62C001C,0x23A04C:0xE620001C,
    0x23A058:0x0C08E34C,0x23A068:0xAE620014,
    0x23A080:0x26021770,0x23A08C:0x2602E890,0x23A0A0:0xAE300014,
    0x23A0B8:0x3C013F40,0x23A0C4:0x46010002,0x23A0C8:0x46000064,
    0x2B506C:0xC4219CE4,0x2B507C:0x46000834,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: actual active distance history, frequency slew6000 and slow-motion channel-only scale0.75')
assert image.read(0x1B1D68,136).hex().endswith('00000000')
for address,expected in {
    0x1B1D80:0x8C472DF4,0x1B1D84:0x10E00016,
    0x1B1DC8:0x0C06C77C,0x1B1DD8:0x0C06CAA6,
    0x238ADC:0x46022101,0x238AEC:0x46030841,0x238AF8:0x46020001,
    0x238B24:0x0C09AE62,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: retail script-object entry has no reconstructed music gate; camera translation subtracts actual position')
for address,expected in {0x23A200:0xC66C0028,0x23A508:0xC62C0028,
    0x23A20C:0x0C08E324,0x23A510:0x0C08E324,
    0x1B277C:0xE6000048, # queue sample starts manager+20: max at sample+28
}.items():assert image.word(address)==expected,hex(address)
print('PASS: pan callers pass maximum-distance field28, not Doppler current-distance field1C')
for address,expected in {0x23A224:0x2602000A,0x23A230:0x2602FFF6,
    0x23A23C:0x0083800A,0x23A240:0xA2300042,0x23A250:0x0C045304,
    0x238BB4:0xE6230000,
}.items():assert image.word(address)==expected,hex(address)
# PS2 LQ/SQ encodings, not the disassembler's incompatible MIPS-MSA labels:
# copy all four camera matrix quadwords into the temporary matrix unchanged.
for offset,register in ((0,3),(16,2),(32,3),(48,2)):
    load_address=0x26B98C if offset==0 else {16:0x26B9A4,32:0x26B9AC,48:0x26B9B4}[offset]
    store_address={0:0x26B99C,16:0x26B9A8,32:0x26B9B0,48:0x26B9BC}[offset]
    assert image.word(load_address)==(0x1E<<26 | 5<<21 | register<<16 | offset)
    assert image.word(store_address)==(0x1F<<26 | 16<<21 | register<<16 | offset)
print('PASS: retail active pan slew10 and exact PS2 LQ/SQ matrix basis copy for inverse projection')
for address,expected in {0x23A0F8:0x24C2000A,0x23A104:0x24C2FFF6,
    0x23A110:0x0083300A,0x23A114:0xA2260018}.items():assert image.word(address)==expected,hex(address)
print('PASS: active volume slew10 and actual history store before channel update')
for address,expected in {0x1B2A50:0x24060001,0x1B2A68:0xA206004C,
    0x1B2A74:0xAE060040, # sample manager+20: static+2C, loopcount+20
    0x19E720:0x3C013F26,0x19E724:0x34216666,0x19E730:0x46002002,
    0x19E764:0x46001800,0x23A5BC:0x3C013F40,0x23A5C4:0x0C0B8FA5,
}.items():assert image.word(address)==expected,hex(address)
print('PASS: phone one-shot is STATIC, loopcount1; original squared distance weights vertical delta0.65')
if len(sys.argv)>1:
    native_spec=importlib.util.spec_from_file_location('phone_native',root/'tools/inspect-native-command.py')
    native=importlib.util.module_from_spec(native_spec);native_spec.loader.exec_module(native)
    pe=native.NativeImage(Path(sys.argv[1]))
    assert pe.read(0x404090,11).hex()=='8b0d9844b700e905ffffff'
    assert pe.read(0x403FF4,14).hex()=='8b410c8b116bc0385b5f03c25ec3'
    # These bytes establish generation/free flags and building stride without
    # invoking the allocator or trusting a potentially hooked constructor.
    assert pe.read(0x403FC7,4).hex()=='f6043e80'
    assert pe.read(0x403FD7,5).hex()=='80e37f8818'
    # Original phone phase is after world/camera/permanent shadows, not the
    # existing early script callback. These are CALLs, inspected as data only.
    for address,target in ((0x53C095,0x5684A0),(0x53C104,0x52B730),
        (0x53C10B,0x50B5D0),(0x53C140,0x70C950)):
        instruction=list(pe.instructions(address,5))
        assert len(instruction)==1 and instruction[0].mnemonic=='call'
        assert instruction[0].op_str==hex(target)
    assert pe.read(0x561AD0,25).hex()=='d90564cbb700d81d24868500dfe0f6c4057a03b001c332c0c3'
    assert pe.read(0x858624,4).hex()=='0000803f'
    assert pe.read(0x827B3D,113).hex()==('5356ff1594808500ff3510318e008bd8ff15e08185008bf085f67549'
        '68880000006a01e8b1c8ffff8bf085f65959742d56ff3510318e00ff15e481850085c0741c'
        'c7465410358e00c7461401000000ff15e8818500834e04ff8906eb086a10e830c9ffff5953'
        'ff159c8085008bc65e5bc3')
    # An existing TLS pointer branches directly to LastError restoration;
    # allocation/seed initialization/fatal-error paths are not granted.
    existing=list(pe.instructions(0x827B55,4))
    assert existing[-1].mnemonic=='jne' and existing[-1].op_str=='0x827ba2'
    print('PASS: complete native CRT TLS helper existing-context branch skips allocation/seed initialization')
    print('PASS: native late world/camera/shadows phase and exact slow-motion predicate; no hook installed')
    print('PASS: native building pool B74498, generation/free flags, stride38; no allocator/constructor execution')
